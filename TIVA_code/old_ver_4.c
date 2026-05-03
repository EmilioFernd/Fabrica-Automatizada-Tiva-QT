//*****************************************************************************
//
// Codigo de partida comunicacion TIVA-QT
// Autores: Eva Gonzalez, Ignacio Herrero, Jose Manuel Cano
//
//  Estructura de aplicacion basica para el desarrollo de aplicaciones genericas
//  basada en la TIVA, en las que existe un intercambio de mensajes con un interfaz
//  grÃ¡fico (GUI) Qt.
//  La aplicacion se basa en un intercambio de mensajes con ordenes e informacion, a traves  de la
//  configuracion de un perfil CDC de USB (emulacion de puerto serie) y un protocolo
//  de comunicacion con el PC que permite recibir ciertas ordenes y enviar determinados datos en respuesta.
//   En el ejemplo basico de partida se implementara la recepcion de un mensaje
//  generico que permite el apagado y encendido de los LEDs de la placa; asi como un segundo
//  mensaje enviado desde la placa al GUI, para mostrar el estado de los botones.
//
//*****************************************************************************
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "inc/hw_memmap.h"       // TIVA: Definiciones del mapa de memoria
#include "inc/hw_types.h"        // TIVA: Definiciones API
#include "inc/hw_ints.h"         // TIVA: Definiciones para configuracion de interrupciones
#include "driverlib/gpio.h"      // TIVA: Funciones API de GPIO
#include "driverlib/pin_map.h"   // TIVA: Mapa de pines del chip
#include "driverlib/rom.h"       // TIVA: Funciones API incluidas en ROM de micro (ROM_)
#include "driverlib/rom_map.h"   // TIVA: Para usar la opciÃ³n MAP en las funciones API (MAP_)
#include "driverlib/sysctl.h"    // TIVA: Funciones API control del sistema
#include "driverlib/uart.h"      // TIVA: Funciones API manejo UART
#include "driverlib/interrupt.h" // TIVA: Funciones API manejo de interrupciones
#include "utils/uartstdioMod.h"  // TIVA: Funciones API UARTSTDIO (printf)
#include "driverlib/adc.h"       // TIVA: Funciones API manejo de ADC
#include "driverlib/timer.h"     // TIVA: Funciones API manejo de timers
#include "drivers/buttons.h"     // TIVA: Funciones API manejo de botones
#include "drivers/rgb.h"         // TIVA: Funciones API manejo de leds con PWM
#include "driverlib/pwm.h"       // TIVA: Funciones API manejo de PWM
#include "FreeRTOS.h"            // FreeRTOS: definiciones generales
#include "task.h"                // FreeRTOS: definiciones relacionadas con tareas
#include "semphr.h"              // FreeRTOS: definiciones relacionadas con semaforos
#include "utils/cpu_usage.h"
#include "commands.h"
#include <serial2USBprotocol.h>
#include <usb_dev_serial.h>
#include "usb_messages_table.h"
#include "config.h"
#include "math.h"
#include "queue.h" //para implementar cola
#include "event_groups.h" //FRERTOS: definiciones para grupos de eventos

#define PWM_BASE_FREQ 10 // Los Hz a los que queremos funcionar

// Variables globales "main"
uint32_t g_ui32CPUUsage;
uint32_t g_ui32SystemClock;
PARAM_RGBDATA datosRGB; //Global para que no perdamos los valores de los distintos RGB
SemaphoreHandle_t mutexUSB, mutexUART; // Para proteccion del canal USB y el caal UART -terminal-, ya que ahora lo van a usar varias tareas distintas
QueueHandle_t cola_prod_cons_1; // Cola para enviar el ID
QueueHandle_t cola_prod_cons_2;
QueueSetHandle_t grupo_colas;
QueueHandle_t mailbox_temperatura;

EventGroupHandle_t FlagEvento;
volatile uint16_t counter;

//Prototipo de funciones
void ADC_Handler(void);
void setRGB(PARAM_RGBDATA datosRGB);
uint32_t brillo_temperatura(uint32_t temperatura);

//DEFINICIón de EVENTOS
#define INICIO_START 0x0001
//*****************************************************************************
//
// The error routine that is called if the driver library encounters an error.
//
//*****************************************************************************
#ifdef DEBUG
void __error__(char *pcFilename, uint32_t ulLine)
{
    while (1) //Si la ejecucion esta aqui dentro, es que el RTOS o alguna de las bibliotecas de perifericos han comprobado que hay un error
    { //Mira el arbol de llamadas en el depurador y los valores de nombrefich y linea para encontrar posibles pistas.
    }
}
#endif

//*****************************************************************************
//
// Aqui incluimos los "ganchos" a los diferentes eventos del FreeRTOS
//
//*****************************************************************************
void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName)
{
    //
    // This function can not return, so loop forever.  Interrupts are disabled
    // on entry to this function, so no processor interrupts will interrupt
    // this loop.
    //
    while (1)
    {
    }
}

void vApplicationTickHook(void)
{
    static uint8_t ui8Count = 0;

    if (++ui8Count == 10)
    {
        g_ui32CPUUsage = CPUUsageTick();
        ui8Count = 0;
    }
    //return;
}

void vApplicationIdleHook(void)
{
    SysCtlSleep();
}

void vApplicationMallocFailedHook(void)
{
    while (1)
        ;
}

//*****************************************************************************
//
// A continuacion van las tareas...
//
//*****************************************************************************

//*****************************************************************************
//
// Codigo de tarea que procesa los mensajes recibidos a traves del canal USB
//
//*****************************************************************************
static portTASK_FUNCTION( USBMessageProcessingTask, pvParameters )
{

    uint8_t pui8Frame[MAX_FRAME_SIZE];
    int32_t i32Numdatos;
    uint8_t ui8Message;
    void *ptrtoreceivedparam;
    uint32_t ui32Errors = 0;

    /* The parameters are not used. */
    (void) pvParameters;

    //
    // Mensaje de bienvenida inicial.
    //
    xSemaphoreTake(mutexUART, portMAX_DELAY);
    UARTprintf(
            "\n\nBienvenido a la aplicacion Fabrica Automatizada (curso 2025/26)!\n");
    UARTprintf("\nAutores: HELENA POSTIGO y EMILIO FERNÁNDEZ ");
    xSemaphoreGive(mutexUART);

    for (;;)
    {
        //Espera hasta que se reciba una trama con datos serializados por el interfaz USB
        i32Numdatos = receive_frame(pui8Frame, MAX_FRAME_SIZE); //Esta funcion es bloqueante
        if (i32Numdatos > 0)
        {	//Si no hay error, proceso la trama que ha llegado.
            i32Numdatos = destuff_and_check_checksum(pui8Frame, i32Numdatos); // Primero, "destuffing" y comprobaciÃ³n checksum
            if (i32Numdatos < 0)
            {
                //Error de checksum (PROT_ERROR_BAD_CHECKSUM), ignorar el paquete
                ui32Errors++;
                // Procesamiento del error 
            }
            else
            { //El paquete esta bien, luego procedo a tratarlo.
                ui8Message = decode_message_type(pui8Frame); //Obtiene el valor del campo mensaje
                i32Numdatos = get_message_param_pointer(pui8Frame, i32Numdatos,
                                                        &ptrtoreceivedparam); //Obtiene un puntero al campo de parametros y su tamanio.
                switch (ui8Message)
                {
                case MENSAJE_PING:
                    //A un mensaje de ping se responde con el propio mensaje
                    i32Numdatos = create_frame(pui8Frame, ui8Message, 0, 0,
                    MAX_FRAME_SIZE);
                    if (i32Numdatos >= 0)
                    {
                        xSemaphoreTake(mutexUSB, portMAX_DELAY);
                        send_frame(pui8Frame, i32Numdatos);
                        xSemaphoreGive(mutexUSB);
                    }
                    else
                    {
                        //Error de creacion de trama: determinar el error y abortar operacion
                        ui32Errors++;
                        // Procesamiento del error
                        // Esto de aqui abajo podria ir en una funcion "createFrameError(numdatos)  para evitar
                        // tener que copiar y pegar todo en cada operacion de creacion de paquete
                        switch (i32Numdatos)
                        {
                        case PROT_ERROR_NOMEM:
                            // Procesamiento del error NO MEMORY
                            break;
                        case PROT_ERROR_STUFFED_FRAME_TOO_LONG:
                            // Procesamiento del error STUFFED_FRAME_TOO_LONG
                            break;
                        case PROT_ERROR_MESSAGE_TOO_LONG:
                            // Procesamiento del error MESSAGE TOO LONG
                            break;
                        case PROT_ERROR_INCORRECT_PARAM_SIZE:
                            // Procesamiento del error INCORRECT PARAM SIZE
                            break;
                        }
                    }
                    break;
                case MENSAJE_INICIO:
                {
                    xEventGroupSetBits(FlagEvento, INICIO_START);

                    PWMGenEnable(PWM0_BASE, PWM_GEN_1); //Pone en marcha el PWM
                    ADCIntClear(ADC0_BASE, 1);//Limpia el Flag
                    ADCSequenceEnable(ADC0_BASE, 1);
                    ADCIntEnable(ADC0_BASE, 1);
                    IntEnable(INT_ADC0SS1);
                    MAP_TimerEnable(TIMER0_BASE, TIMER_A);


                    break;

                }

                default:
                {
                    PARAM_MENSAJE_NO_IMPLEMENTADO parametro;
                    parametro.message = ui8Message;
                    //El mensaje esta bien pero no esta implementado
                    i32Numdatos = create_frame(pui8Frame,
                                               MENSAJE_NO_IMPLEMENTADO,
                                               &parametro, sizeof(parametro),
                                               MAX_FRAME_SIZE);
                    if (i32Numdatos >= 0)
                    {
                        xSemaphoreTake(mutexUSB, portMAX_DELAY);
                        send_frame(pui8Frame, i32Numdatos);
                        xSemaphoreGive(mutexUSB);
                    }
                    break;
                }
                }                        // switch
            }
        }
        else
        { // if (ui32Numdatos >0)
//Error de recepcion de trama(PROT_ERRProductorOR_RX_FRAME_TOO_LONG), ignorar el paquete
            ui32Errors++;
// Procesamiento del error
        }
    }
}

static portTASK_FUNCTION(productoraGenerica, pvParameters)
{
    xEventGroupWaitBits(FlagEvento, INICIO_START, pdFALSE, pdTRUE,
    portMAX_DELAY); //Espera al botón de inicio
    PARAM_PRODUCTORA *param = (PARAM_PRODUCTORA*) pvParameters;
    INFO_COLA info_cola;
    int32_t N = 10000;
    info_cola.IDProd = param->IDProd;

    while (1)
    {
        info_cola.id = (rand() % N) + 1;
        vTaskDelay(param->retardo * configTICK_RATE_HZ);
//MAP_GPIOPinWrite(GPIO_PORTF_BASE, param->ui8Pins,param->ui8Pins);// modo GPIO
        if (param->IDProd == 1)
        {
            datosRGB.R = 0xFFFF;
            setRGB(datosRGB); //MODO RGB
            xQueueSend(cola_prod_cons_1, &info_cola, portMAX_DELAY);
            datosRGB.R = 0x0;
            setRGB(datosRGB); //MODO RGB
        }
        if (param->IDProd == 2)
        {
            datosRGB.G = 0xFFFF;
            setRGB(datosRGB); //MODO RGB
            xQueueSend(cola_prod_cons_2, &info_cola, portMAX_DELAY);
            datosRGB.G = 0x0;
            setRGB(datosRGB); //MODO RGB
        }
//MAP_GPIOPinWrite(GPIO_PORTF_BASE, param->ui8Pins, 0); //Modo GPIO

    }
}

static portTASK_FUNCTION(consumidora1,pvParameters)
{
    uint8_t pui8Frame[MAX_FRAME_SIZE];
    int32_t i32Numdatos;
    INFO_COLA info_cola;
    QueueSetMemberHandle_t cola_activada;

    while (1)
    {
        cola_activada = xQueueSelectFromSet(grupo_colas, portMAX_DELAY);

        if (cola_activada == cola_prod_cons_1)
        {
            xQueueReceive(cola_prod_cons_1, &info_cola, 0);
            vTaskDelay(2 * configTICK_RATE_HZ);
            counter++;
        }
        if (cola_activada == cola_prod_cons_2)
        {
            xQueueReceive(cola_prod_cons_2, &info_cola, 0);
            vTaskDelay(5 * configTICK_RATE_HZ);
            counter++;
        }

        PARAM_MENSAJE_CONTADOR parametro;
        parametro.numMensajes = counter; // counter es global
        parametro.id = info_cola.id; //lee el kid ID de la tarea productora
        parametro.IDProd = info_cola.IDProd; //lee el id de la productora
//El mensaje esta bien pero no esta implementado
        i32Numdatos = create_frame(pui8Frame, MENSAJE_CONTADOR, &parametro,
                                   sizeof(parametro), MAX_FRAME_SIZE);
        if (i32Numdatos >= 0)
        {
            xSemaphoreTake(mutexUSB, portMAX_DELAY);
            send_frame(pui8Frame, i32Numdatos);
            xSemaphoreGive(mutexUSB);
        }
    }
}

static portTASK_FUNCTION(temperatura,pvParameters)
{
    uint8_t pui8Frame[MAX_FRAME_SIZE];
    PARAM_MENSAJE_TEMPERATURA datosTemperatura;
    int32_t i32Numdatos;

    while (1)
    {
        xQueuePeek(mailbox_temperatura, &datosTemperatura, portMAX_DELAY);
        datosTemperatura.ambiente = (1475
                - ((2475 * datosTemperatura.ambiente)) / 4096) / 10; //Fórmula para sensor interno, aparece en el datasheet
        datosTemperatura.soldadura = 300 + ((datosTemperatura.soldadura * 150) / 4095); //Cuando el valor máximo es 3.3V vamos obtener un valor de 4095

        // Si la temperatura es máxima (450) máximo valor de led, si la temperatura es mínima(300) mínimo valor de led.
        datosRGB.B = brillo_temperatura(datosTemperatura.soldadura);
        setRGB(datosRGB);

        i32Numdatos = create_frame(pui8Frame, MENSAJE_TEMPERATURA,
                                   &datosTemperatura, sizeof(datosTemperatura),
                                   MAX_FRAME_SIZE);
        if (i32Numdatos >= 0)
        {
            xSemaphoreTake(mutexUSB, portMAX_DELAY);
            send_frame(pui8Frame, i32Numdatos);
            xSemaphoreGive(mutexUSB);
        }
    }

}

//FUNCIONES AUXILIARES
//---------------------------
//---------------------------

// Establece el valor de RED BLUE y GREEN y la intensidad
// R, B y G son variables uint32_t
// intenisty es float que tiene que ir de 0.0 a 1.0
void setRGB(PARAM_RGBDATA datosRGB)
{
    uint32_t color[3];
    color[0] = datosRGB.R;
    color[1] = datosRGB.G;
    color[2] = datosRGB.B;
    RGBSet(color, datosRGB.intensity);
}

//============================
// Función que devuelve el valor en uint32_t para el led correspondiente según el valor de temperatura
// registrado por la placa.
// 300 grados establece un valor de 0
// 450 grados establece un valor de 0xFFFF; 65535
// Valores intermedios realizamos una función lineal
// uint32_t temperatura: es el valor de la temperatura registrada de forma externa mediante ADC
uint32_t brillo_temperatura(uint32_t temperatura){
    if (temperatura <= 300) return 0;
    if (temperatura >= 450) return 0xFFFF;
    return ((temperatura - 300) * 65535) / 150;
}

//*****************************************************************************
//
// Funcion main(), Inicializa los perifericos, crea las tareas, etc... y arranca el bucle del sistema
//
//*****************************************************************************
int main(void)
{

    //Parámetros de tareas
    static PARAM_PRODUCTORA prod1;
    prod1.IDProd = 1;
    prod1.retardo = 2;
    //prod1.ui8Pins = GPIO_PIN_1; //rojo

    static PARAM_PRODUCTORA prod2;
    prod2.IDProd = 2;
    prod2.retardo = 1; //segundos
    //prod2.ui8Pins = GPIO_PIN_3; //verde

    //Variables PWM
    uint32_t pwm_clk; //representa la frecuencia del reloj del PWM
    uint32_t val_load; // Variable a cargar en el generador PWM para conseguir la frecuencia PWM_BASE_FREQ, usando como reloj del generador PWM, pwm_clk
    uint8_t duty = 50; // variable auxiliar para modificar el ciclo de trabajo entre el 1% y el 100%

    //
    // Reloj del sistema definido a 40MHz
    //
    MAP_SysCtlClockSet(
    SYSCTL_SYSDIV_5 | SYSCTL_USE_PLL | SYSCTL_XTAL_16MHZ | SYSCTL_OSC_MAIN);

    //Establecer el reloj generador PWM (40MHz/64 = 625KHz)
    SysCtlPWMClockSet(SYSCTL_PWMDIV_64);

    // Habilitar módulos y puerto de salida
    SysCtlPeripheralEnable(SYSCTL_PERIPH_PWM0); //Para le PB4
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);

    //Habilitación módulo leds
    //SysCtlPeripheralEnable(SYSCTL_PERIPH_PWM1); //Habilita modulo PWM
    //SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);    // Habilita puerto salida para señal PWM (ver en documentacion que pin se corresponde a cada módulo PWM)

    //Habilitamos PB4
    GPIOPinTypePWM(GPIO_PORTB_BASE, GPIO_PIN_4);
    GPIOPinConfigure(GPIO_PB4_M0PWM2); // del módulo PWM1 (ver tabla 10.2 Data-Sheet, columnas 4 y 5)

    pwm_clk = SysCtlClockGet() / 64; //Frecuencia de la onda PWM (625Khz)
    val_load = (pwm_clk / PWM_BASE_FREQ) - 1;

    PWMGenConfigure(PWM0_BASE, PWM_GEN_1, PWM_GEN_MODE_DOWN);
    PWMGenPeriodSet(PWM0_BASE, PWM_GEN_1, val_load);
    PWMPulseWidthSet(PWM0_BASE, PWM_OUT_2, duty * val_load / 100); //50/100 = 0.5
    PWMOutputState(PWM0_BASE, PWM_OUT_2_BIT, true); // Habilita la salida de la señal

    //-----------------------------------------------------------------
    //----CONFIGURACION ADC--------------------------------------------
    //-----------------------------------------------------------------
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0); // Habilita el ADC0
    //Deshabilitar para configurar
    ADCSequenceDisable(ADC0_BASE, 1); //Secuenciador 1

    //Configuramos el Sobremuestro
    ADCHardwareOversampleConfigure(ADC0_BASE, 64);

    ADCSequenceConfigure(ADC0_BASE, 1, ADC_TRIGGER_TIMER, 0);
    ADCSequenceStepConfigure(ADC0_BASE, 1, 0, ADC_CTL_TS); //sensor interno de la TIVA
    ADCSequenceStepConfigure(ADC0_BASE, 1, 1,
                             ADC_CTL_CH0 | ADC_CTL_IE | ADC_CTL_END);

    IntRegister(INT_ADC0SS1, ADC_Handler);
    //Añadimos prioridades
    IntPrioritySet(INT_ADC0SS1, 0xE0); //Prioridad más baja según manual
    IntPrioritySet(INT_USB0, 0xE0);
    ADCIntClear(ADC0_BASE, 1);


    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    MAP_TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);
    MAP_TimerLoadSet(TIMER0_BASE, TIMER_A, SysCtlClockGet() - 1);
    MAP_TimerControlTrigger(TIMER0_BASE, TIMER_A, true);



    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE); //habilito el puerto E
    MAP_GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_3); //configurar el pin como ADC


    //HABILITAMOS

    // Obtiene el reloj del sistema
    g_ui32SystemClock = SysCtlClockGet();

    //Habilita el clock gating de los perifericos durante el bajo consumo --> perifericos que se desee activos en modo Sleep
    //                                                                        deben habilitarse con SysCtlPeripheralSleepEnable
    MAP_SysCtlPeripheralClockGating(true);
    MAP_SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_PWM0);
    MAP_SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_GPIOB);
    MAP_SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_GPIOF);
    MAP_SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_PWM1);
    MAP_SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_USB0);
    MAP_SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_ADC0);
    MAP_SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_TIMER0);
    MAP_SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_GPIOE);

    //Habilitamos Leds
    RGBInit(1); //inilizaciza los leds
    RGBEnable();
    datosRGB.intensity = 1.0;
    //Habilitamos puerto
    // Habilita el periférico: Puerto F (LEDs)
    //MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    // Configura pines PF1 como salidas (control de LEDs)
    //MAP_GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1); //ROJO
    //MAP_GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_3); //VERDE

    // Inicializa el subsistema de medida del uso de CPU (mide el tiempo que la CPU no esta dormida)
    // Para eso utiliza un timer, que aqui hemos puesto que sea el TIMER3 (ultimo parametro que se pasa a la funcion)
    // (y por tanto este no se deberia utilizar para otra cosa).
    CPUUsageInit(g_ui32SystemClock, configTICK_RATE_HZ / 10, 3);

    /**                                              Creacion de tareas 									**/
    // Inicializa el sistema de depuraciÃ³n e interprete de comandos por terminal UART
    if (initCommandLine(256, tskIDLE_PRIORITY + 1) != pdPASS)
    {
        while (1)
            ;
    }

    USBSerialInit(32, 32);	//Inicializo el  sistema USB
    //
    // Crea la tarea que gestiona los mensajes USB (definidos en USBMessageProcessingTask)
    //
    if (xTaskCreate(USBMessageProcessingTask, "usbser", 512, NULL,
    tskIDLE_PRIORITY + 2,
                    NULL) != pdPASS)
    {
        while (1)
            ;
    }

    // CREACIÓN DEL GRUPO DE FLAGS
    FlagEvento = xEventGroupCreate();
    if (NULL == FlagEvento)
    {
        while (1)
            ;
    }

    //
    // A partir de aqui se crean las tareas de usuario, y los recursos IPC que se vayan a necesitar
    //

    mutexUART = xSemaphoreCreateMutex();
    if (NULL == mutexUART)
        while (1)
            ;

    mutexUSB = xSemaphoreCreateMutex();
    if (NULL == mutexUSB)
        while (1)
            ;
    //
    // Pone en marcha el planificador. La llamada NO tiene retorno
    //




    cola_prod_cons_1 = xQueueCreate(3, sizeof(INFO_COLA));
    if (NULL == cola_prod_cons_1)
    {
        while (1)
            ;

    }

    cola_prod_cons_2 = xQueueCreate(3, sizeof(INFO_COLA));
    if (NULL == cola_prod_cons_2)
    {
        while (1)
            ;

    }

    grupo_colas = xQueueCreateSet(3 + 3);
    if (NULL == grupo_colas)
    {
        while (1)
            ;
    }

    mailbox_temperatura = xQueueCreate(1, sizeof(PARAM_MENSAJE_TEMPERATURA));
    if (NULL == mailbox_temperatura)
    {
        while (1)
            ;
    }

    //Se crea, Añadimos las dos colas al grupo de colas
    if (xQueueAddToSet(cola_prod_cons_1, grupo_colas) != pdPASS)
    {
        while (1)
            ;
    }
    if (xQueueAddToSet(cola_prod_cons_2, grupo_colas) != pdPASS)
    {
        while (1)
            ;
    }

    //================================================================
    //======CREACIÓN DE TAREAS========================================
    //================================================================


    if ((xTaskCreate(productoraGenerica, "Productor1", 128, &prod1,
    tskIDLE_PRIORITY + 1,
                     NULL) != pdPASS))
    {
        while (1)
        {

        }
    }

    if ((xTaskCreate(productoraGenerica, "Productor2", 128, &prod2,
    tskIDLE_PRIORITY + 1,
                     NULL) != pdPASS))
    {
        while (1)
        {

        }
    }

    if ((xTaskCreate(consumidora1, "Consumidor", 128, NULL,
    tskIDLE_PRIORITY + 1,
                     NULL) != pdPASS))
    {
        while (1)
        {

        }
    }

    if ((xTaskCreate(temperatura, "Temperatura", 128, NULL,
                     tskIDLE_PRIORITY + 1,
                     NULL) != pdPASS))
    {
        while (1)
        {

        }
    }

    //================================================================
    //======FIN DE TAREAS=============================================
    //================================================================

    vTaskStartScheduler(); //el RTOS habilita las interrupciones al entrar aqui, asi que no hace falta habilitarlas

    //De la funcion vTaskStartScheduler no se sale nunca... a partir de aqui pasan a ejecutarse las tareas.
    while (1)
    {
//Si llego aqui es que algo raro ha pasado
    }
}

//-------------------------------------------
//-----RUTINAS DE INTERRUPCIONES-------------
//-------------------------------------------

void ADC_Handler(void)
{
    ADCIntClear(ADC0_BASE, 1); //bajar flag
    uint32_t ui32ADC0Value[2];
    // Leemos los datos del secuenciador
    ADCSequenceDataGet(ADC0_BASE, 1, ui32ADC0Value);

    PARAM_MENSAJE_TEMPERATURA datos_temp;
    datos_temp.ambiente = ui32ADC0Value[0];
    datos_temp.soldadura = ui32ADC0Value[1];

    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xQueueOverwriteFromISR(mailbox_temperatura, &datos_temp,
                           &higherPriorityTaskWoken);
    portEND_SWITCHING_ISR(higherPriorityTaskWoken);

}
