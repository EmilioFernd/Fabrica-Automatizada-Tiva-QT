<img width="1373" height="895" alt="image" src="https://github.com/user-attachments/assets/18eb61e2-6f52-4553-aae1-415bd526bc7d" />

# Documentación Código Proyecto Fábrica

## 1. Creación de Productora y Consumidora

### 1.1. Creación `productoraGenerica`

Creamos la tarea `productoraGenerica` que es una plantilla usada para *productora1* y *productora2*.

```c
static portTASK_FUNCTION(productoraGenerica, pvParameters)
```

La tarea productora tiene como parámetros el ID de cada producto `IDProd` y el tiempo que tardan en crear el kit `retardo`.

```c
    static PARAM_PRODUCTORA prod1;
    prod1.IDProd = 1;
    prod1.retardo = 3; //segundos

    static PARAM_PRODUCTORA prod2;
    prod2.IDProd = 2;
    prod2.retardo = 4; //segundos
```

Cada tarea posee su **propia cola** a través cada una envía `IDProd` y `retardo` para poder comunicarse con la *Consumidora*.
```C
QueueHandle_t cola_prod_cons_1; 
QueueHandle_t cola_prod_cons_2;
```

Cada *productora* activará su **led**:

-  *productora1* → LED ROJO
-  productora2 →  LED VERDE

Cuando la cola esté **llena**, y por tanto, no pueda escribir en ella.



### 1.2. Creación `consumidora1`

Recibe los parámetros de las *productoras* mediante las colas (`cola_prod_cons_1`,`cola_prod_cons_2`) y tras un **retraso** (*tiempo que tarda en ensamblar los productos*), los envía a *QT* mediante los parámetros  almacenados en `MENSAJE_CONTADOR`. 

```C
static portTASK_FUNCTION(consumidora1,pvParameters)
```

La tarea *consumidora* también será la encargada de implementar ***counters*** que envían junto con los parámetros del kit de *productora*. Que muestra información sobre el total de productos ensamblados y el total de cada tipo de productos.



#### 1.2.1 Ensamblaje de diferentes productos

Cada productora posee su propia cola (`cola_prod_cons_1`,`cola_prod_cons_2`), estas se ven reguladas mediante un **grupo de colas** `grupo_colas`.
```c 
QueueSetMemberHandle_t cola_activada;
cola_activada = xQueueSelectFromSet(grupo_colas, portMAX_DELAY);
```

Crea dos variables *counters* `counter_prod_1` `counter_prod_2` diferenciando la cantidad de productos ensamblados provenientes de cada cola.

 

### 1.3. Widgets Qt 

Recibe `MENSAJE_CONTADOR` procedente de tarea `consumidora1` y la información será mostrada mediante los widgets del apartado de *producción*.

Para ello creamos un nuevo `case` dentro de la función de lectura:

```c
case MENSAJE_CONTADOR:
                    {
                        PARAM_MENSAJE_CONTADOR parametro;
                        if (check_and_extract_message_param(ptrtoparam, tam, sizeof(parametro),&parametro)>0)
                        {
                            //...
```

<img width="750" height="327" alt="image-20260507120058685" src="https://github.com/user-attachments/assets/dd4f283a-c2d1-4113-a9d0-d68db3457e32" />


---



## 2. Arranque de la fábrica

### 2.1. Creación del Botón `Inicio`

Implementa en *QT*  botón inicio que envía `MENSAJE_INICIO` a la TIVA cuando es presionado. Se implementa mediante `void GUIPanel::on_inicio_pressed()`, este botón se **deshabilitará** cuando se presione al inicio del programa. 

 Una vez recibido, se activa el *flag* `INICIO_START` del grupo `FlagEvento`, lo cual activa las *productoras* que estaban esperando este *flag*.
```c
EventGroupHandle_t FlagEvento;
#define INICIO_START 0x0001
```

### 2.2. Onda PWM

El botón inicio habilita una onda **PWM** (a 10HZ y con ciclo de trabajo del 50%) que enciende un **led externo** mediante el pin `PB4`.
La onda PWM se configura en el `main` del programa, no obstante, se activa cuando llega `MENSAJE_INICIO` a la TIVA, en la rutina de recepción del mensaje.

``` c
case MENSAJE_INICIO:
                {
                 PWMGenEnable(PWM0_BASE, PWM_GEN_1);
```

---



## 3. Creación de Tarea Temperatura

Crea la tarea `Temperatura` para gestionar los valores de temperatura procedentes de la medición con **ADC** de sensor interno como de un pin externo. 

```c
static portTASK_FUNCTION(temperatura,pvParameters)
```



### 3.1. Configuración del ADC

Usa `ACD0` y secuenciador 1. El *trigger* es controlado mediante el *timeout* del `TIMER4`. Tiene un **periodo de muestreo** de $500 ms$ (mejor fluidez). 
EL **ADC** leerá tanto el sensor interno de la TIVA como el puerto E, pin 3, `PE3`, que estará conectado a un **potenciómetro** para variar sus valores.

Mediante la *RTI* del ADC
```c
void ADC_Handler(void)
```

Recibe los valores:

-  `ambiente` → sensor interno
-  `soldadura` → `PE3`

Almacenados en el mensaje  `MENSAJE_TEMPERATURA` y se envía a la tarea *temperatura*, mediante el uso del *mailbox* `mailbox_temperatura`. 

```c
QueueHandle_t mailbox_temperatura;
```



### 3.2. Configuración tarea `temperatura`

Tarea `temperatura` procesa los datos recibidos por `mailbox_temperatura` mediante un `QueuePeek` y los transforma en valores en **grados (°C)**.
Gestiona la intensidad **LED AZUL** en función a la temperatura `soldadura` a través de la *librería RGB* y de una **función auxiliar**: (*300°C led apagado, 450°C máxima intensidad*)

```c
uint32_t brillo_temperatura(uint32_t temperatura)
{
    if (temperatura <= 300)
        return 0;
    if (temperatura >= 450)
        return 0xFFFF;
    return ((temperatura - 300) * 65535) / 150;
}
```



### 3.3 Widgets QT

La tarea `temperatura` envía a *QT* los valores en **grados** mediante los parámetros de `MENSAJE_TEMPERATURA`. 
*QT* recibe los mensajes en un nuevo `case` de la rutina de recepción de mensaje y actualiza los widgets del grupo *temperaturas*

<img width="468" height="325" alt="image-20260507125220835" src="https://github.com/user-attachments/assets/b3fd53b4-e95f-4e05-8810-de7be7ad9eee" />


---

## 4. Alarmas y Control del Sistema.

Crea la tarea `tareaControl` para gestionar ciertas situaciones críticas mediante un **grupo de flags** que se llama `FlagAnomalia`.

```c
static portTASK_FUNCTION(tareaControl, pvParameters)
```

```c
EventGroupHandle_t FlagAnomalia;
#define COLA_LLENA_PROD_1 0x0001     //0b000001
#define COLA_LLENA_PROD_2 0x0002     //0b000010
#define TIME_STOP 0x0004             //0b000100
#define TEMP_HAZARDOUS 0x0008        //0b001000
#define TEMP_HAZARDOUS_OFF 0x0010    //0b010000
```

El grupo de *flags* está bloqueado esperando *flags* activos. 

```c
ui32FlagsAnomalia = xEventGroupWaitBits(
    FlagAnomalia,
    COLA_LLENA_PROD_1 | COLA_LLENA_PROD_2 | TIME_STOP
    | TEMP_HAZARDOUS | TEMP_HAZARDOUS_OFF,
    pdTRUE, pdFALSE, portMAX_DELAY);
```



### 4.1. Cola de *productora* llena

La implementación para la cola de *productora 1* y *productora 2* son equivalentes, por lo que se explicará *productora 1*.

El *flag* `COLA_LLENA_PROD_1`se activa mediante el bloqueo al enviar por la cola en la tarea *productora*. Se usa un semáforo `semaforo_prod_1` para bloquearla hasta que nosotros lo indiquemos.

```c
while (xQueueSend(cola_prod_cons_1, &info_cola, 0) == pdFALSE)
            { 
                g_ui8_prod1_block = 1;
                xEventGroupSetBits(FlagAnomalia, COLA_LLENA_PROD_1);
                xSemaphoreTake(semaforo_prod_1, portMAX_DELAY); 
```

En la *tarea control*, al activarse el *flag* se activa el **LED** de la TIVA, para indicar la situación de bloqueo.

-  *productora1* → LED ROJO
-  productora2 →  LED VERDE



#### 4.1.1. Widgets QT

La tarea envía a *QT* mediante el mensaje `MENSAJE_BLOQUEO` la información de que cuál productora se ha bloqueado, y lo refleja en un *widget LED*. Recibo `MENSAJE_BLOQUEO` por *QT* en un nuevo `case` y actualizo los valores de los LEDS del grupo *bloqueos*. 

<img width="243" height="167" alt="image-20260507131800118" src="https://github.com/user-attachments/assets/a5f921af-1d04-4717-a9b8-b11e1554ce72" />




#### 4.1.2. Comando `reanudar <num>`

En `commands.c`  implementa un nuevo comando `reanudar <num>` que **libera** (*give*) los semáforos bloqueantes de las *productoras*.

```c
static int Cmd_reanudar(int argc, char *argv[])
{//...
```

Comprobando el número correcto de entradas, que se haya escrito de forma correcta e identificando y liberando el semáforo correspondiente según si el número introducido es `1 o 2`

También es capaz de detectar si se intenta llamar a la función cuando está no estaba bloqueada, mediante la variable `g_ui8_prod1_block, g_ui8_prod2_block`. Mostrando un mensaje diferente por *UART* según el caso. 

```c
UARTprintf("Intentando reanudar productora 1\r\n");
UARTprintf("La tarea productora 1 no estaba bloqueada \r\n");
```



### 4.2. Parada Temporal de planta de fabricación

#### 4.2.1. Configuración del switch

Usa la librería `buttons` de la TIVA, para inicializar y activar las interrupciones del botón `derecho`, `RIGHT_BUTTON`. 

```c
void GPIOF_Handler(void)
```

La interrupción del *switch* se activa en el **flanco de bajada**, cuando entre en la *RTI*; se configura para detectar **flanco de subida** y activará el *flag* `TIME_STOP` para poder acceder a su rutina en la tarea `TareaControl` .



#### 4.1.2. Configuración `Timer2`

Inicializa el `Timer2` para contar 20 segundos. El *timer* tiene asociado la *RTI* :

```c
void TIMER2A_Handler(void)
```

Utiliza un *grupo de flags*:

```c
EventGroupHandle_t FlagBoton;
#define BOTON_DESACTIVADO 0x0001
```

El *flag* `BOTON_DESACTIVADO` se **inicializa activado** para no bloquear las tareas *productoras* que están a la espera de la llegada de este.

 ```c
  xEventGroupWaitBits(FlagBoton, BOTON_DESACTIVADO, pdFALSE, pdFALSE, portMAX_DELAY);
 ```

En la *RTI* del `TIMER2` activamos el *flag* `BOTON_DESACTIVADO` que libera las productoras del bloqueo cuando se finaliza el timer.



#### 4.1.3. Configuración tarea `tareaControl` - *flag* `TIME_STOP`

La activación del *flag* `TIMER_STOP` produce en la tarea la **desactivación** del flag `BOTON_DESACTIVADO` bloqueando las *productoras*, y la **activación** del `TIMER2`.



#### 4.1.4 Widgets QT

Para implementar una cuenta atrás en *QT* crea un Timer SW `timerParada` que cuenta cada 1 segundo y va disminuyendo su `ID` por cada expiración del TIMER y se envía al QT que actualiza el valor de *display* en el grupo *parada*

<img width="378" height="156" alt="image-20260507180626880" src="https://github.com/user-attachments/assets/aa44edad-3de5-48a9-8b8d-beea9a52d2e0" />




### 4.3 Temperatura de soldadura en umbral peligroso

#### 4.3.1. Tarea `temperatura`

Tarea `temperatura` detecta los siguientes casos:

-  Supera los 410 °C → Activa el *flag* `TEMP_HAZARDOUS`
-  Baja de 400 °C → Activa el flag `TEMP_HAZARDOUS_OFF`

También se comunica con la tarea `tareaControl` mediante un ***mailbox***  `mailbox_temperatura_control` para enviar información de las temperaturas mediante el mensaje `MENSAJE_TEMPERATURA`.



#### 4.3.2. Configuración `tareaControl` - *flag* `TEMP_HAZARDOUS`

La activación de *flag* `TEMP_HAZARDOUS` provoca la activación de un timer *SW* `timerApagadoTemperatura`. El timer realiza una cuenta de 30 segundos, modificando su valor de *ID* desde 30 hasta 0, saltando cada 1 segundo. 

```c
timerApagadoTemperatura = xTimerCreate("Cuenta30seg", pdMS_TO_TICKS(1000),
                                       pdTRUE, (void*) 30, Timer1CallBack); //Contará 30 veces 1 segundo
```

El parámetro `temp_hazardous` se activará siempre que se active el timer de 30 segundos para evitar la activación de timer cada vez que el valor `soldadura` supera los $410°C$.Del mismo modo, habilita el **parpadeo** de los LEDS de la TIVA siempre que el timer *SW* se encuentre activo.

```c 
RGBBlinkRateSet(1.0f);
```

Cuando `timerApagadoTemperatura` llegue a fin de cuenta se activará el parámetro `war_temp_critico` que avisará a QT de la suspensión de la fábrica, además de permitir la suspensión de todas las tareas.

```c
if (suspender_fabrica == 1) // Timer acabado
        {
            vTaskSuspend(handleProductora1);
            vTaskSuspend(handleProductora2);
            vTaskSuspend(handleConsumidora);
            vTaskSuspend(handleTemperatura);
            vTaskSuspend(NULL); // porque estamos dentro de la tarea control
        }
```



#### 4.3.3. Configuración `tareaControl` - *flag* `TEMP_HAZARDOUS_OFF`

La activación de *flag* `TEMP_HAZARDOUS_OFF` provoca la activación de un timer *SW* `timerTresSeg`. El timer realiza una cuenta de 3 segundos, modificando su valor de *ID* desde 6 hasta 0, saltando cada 500 milisegundos, para sondear posibles variaciones de la temperatura entre valores críticos y normales durante el funcionamiento y reiniciar los 3 segundos si se vuelve a una temperatura normal. 
```c
timerTresSeg = xTimerCreate("Cuenta3seg", pdMS_TO_TICKS(500), pdTRUE,
                            (void*) 6, Timer2CallBack); //Cuenta 500 ms 6 veces = 3 segundos 
```

Si se pasan los 3 segundos sin que se suba la temperatura, se para el timer `timerApagadoTemperatura`, se reinicia a los 30 segundos y el programa desactiva todos los parámetros que avisan de la activación de temperatura peligrosa `temp_hazardous`.



#### 4.3.4 Widgets QT

Recibe mediante el *case* de `MENSAJE_ANOMALIAS` el parámetro `temp_hazardous` que activa un *LED* cada vez que la temperatura es crítica, también recibe el contador del timer de 30 segundos `cuenta_atras`. 
Cuando recibe el parámetro `war_temp_critico` **deshabilita** los widgets y aparece en la interfaz una **ventana emergente** avisando del estado crítico de la fábrica


<img width="702" height="136" alt="image-20260507185029261" src="https://github.com/user-attachments/assets/30d08175-fe22-46a0-a9fa-6366f40c488d" />
<img width="347" height="180" alt="image-20260507185128726" src="https://github.com/user-attachments/assets/d5d39c64-3ed4-4fa8-86c8-343fb1267a9b" />


---

## 5. Objetivos de Fabricación

## 5.1 Widgets QT

Usa `QwtKnob` para introducir el número de **objetivos**, mediante un `go to slots` que se ejecuta cada vez que cambiamos el valor del *widget*.
El *widget* mediante las funciones `setEnable` desactiva el botón de `inicio` hasta que no carga un valor mayor a $0$ . Una vez activa la fábrica se deshabilita el *widget* `QwtKnob` para evitar su modificación. El valor de los objetivos es enviado a la TIVA mediante el mensaje `MENSAJE_INICIO` y el parámetro `objetivos`.

<img width="457" height="343" alt="image-20260507185847170" src="https://github.com/user-attachments/assets/32b04da8-f34a-4dad-b483-596b274aea40" />




## 5.2 Creación semáforo contador 

La TIVA recibe el mensaje `MENSAJE_INICIO` y se crea un **semáforo contador** inicializándolo con el número de objetivos recibido.
```c
semaforo_cont_prod = xSemaphoreCreateCounting(100, parametros.objetivos); 
```

Cada vez que *consumidora* detecte algo de la cola cogerá el semáforo contador. También se disminuye el valor de `counter_objetivo` que indicará a *QT* el número de objetivos restantes mediante el mensaje `MENSAJE_CONTADOR`.



### 5.3 Creación grupo de *flags* `FlagObjetivo`

Crea el grupo de *flags* inicializado con `OBJETIVO_NO_CUMPLIDO` activo para evitar el bloqueo de las productoras.

```c
EventGroupHandle_t FlagObjetivo;
#define OBJETIVO_NO_CUMPLIDO 0x0001
```

Cuando el semáforo contador `semaforo_cont_prod` se bloquee, desactivamos el *flag* `OBJETIVO_NO_CUMPLIDO`.

 ```c
 xEventGroupWaitBits(FlagObjetivo, OBJETIVO_NO_CUMPLIDO, pdFALSE,
                     pdFALSE, portMAX_DELAY);
 ```

---

## 6. Traza y Depuración de la aplicación

### 6.1. Activar y desactivar un modo traza del sistema

#### 6.1.1. Comando `traza <on/off>`

Crea un comando `Cmd_traza` en `commands.c` que activa un *flag* `TRAZA_ACTIVA` del grupo de *flags* `Flags_Traza`.
```c
static int Cmd_traza(int argc, char *argv[])
{//...
```

El comando avisa por UART si se ha activado o desactivado el modo, o si el uso ha sido incorrecto.



#### 6.1.2. Grupo de *flags* `FlagTrazas`

Implementa un grupo de *flags* **no bloqueantes** que se comprobarán si se han activado en cada tarea que se quiera tener una traza.

```c
EventGroupHandle_t FlagTrazas;
#define TRAZA_ACTIVA 0x0001
```

Usa la función auxiliar `TrazasActivas` que devuelve el valor $1$ o $0$ si el *flag* se ha activado, para la facilitación de implementación de código.
```c
static uint8_t TrazasActivas(void)
{
    EventBits_t bits;
    //Consultamos los bits de grupo de FLAG
    bits = xEventGroupGetBits(FlagTrazas);
    //Realiza una comprobación si traza está activada
    return ((bits & TRAZA_ACTIVA) == TRAZA_ACTIVA);
}
```

Ejemplo: 

```c
if (TrazasActivas())
            {
                xSemaphoreTake(mutexUART, portMAX_DELAY);
                UARTprintf("Traza: alarma, cola llena en productora 1\r\n");
                xSemaphoreGive(mutexUART);
            }
```

Importancia de la necesitada de usar `mutex`para proteger la *UART* ya que la escribimos desde diferentes tareas.

<img width="548" height="342" alt="image-20260507192901199" src="https://github.com/user-attachments/assets/8b9945d4-606e-4d29-b1ce-e61f166b02e5" />



### 6.2 Modificar alguna variable o elemento de sistema

#### 6.2.1 Comando `modifica cuenta30 <xx>`

Crea un comando `Cmd_modifica` en `commands.c` que modifica el valor de la cuenta atrás de `timerApagadoTemperatura` mediante la comunicación por un ***mailbox***

```c
static int Cmd_modifica(int argc, char *argv[])
{//...
```

El comando solo avisa por *UART* si el comando ha tenido algún fallo en la implementación. 



#### 6.2.2 Creación del *mailbox* `mailbox_modifica`

Crea un *mailbox* para poder comunicar el comando con la tarea `tareaControl`

```c
mailbox_modifica = xQueueCreate(1, sizeof(uint8_t));
```

Dentro del flag `TEMP_HAZARDOUS` de la tarea `tareaControl` leemos mediante un `xQueueRecieve` para leer y liberar el dato del *mailbox* y le asignamos el valor al *ID* de `timerApagadoTemperatura`. 
Sólo puede modificar el valor de cuenta atrás mientras el *timer* esté activo. 

<img width="223" height="22" alt="image-20260507194104878" src="https://github.com/user-attachments/assets/88ec272f-f6c7-443d-bc1b-503d2b0f2227" />
<img width="692" height="128" alt="image-20260507194047238" src="https://github.com/user-attachments/assets/3a65bbb8-fbc1-466d-bc98-20aedf993c63" />


---





# Tabla mensajes empleados



| Mensaje                   | Código |                           Función                            | Parámetros                                                   | Transmisión           |
| ------------------------- | ------ | :----------------------------------------------------------: | ------------------------------------------------------------ | --------------------- |
| `MENSAJE_NO_IMPLEMENTADO` | $0x00$ | El microcontrolador recibe un mensaje no implementado o desconocido debe responder con este mensaje | mensaje desconocido                                          | TIVA →QT              |
| `MENSAJE_PING`            | $0x01$ |      El microcontrolador responde a una petición de eco      | No tiene                                                     | QT→TIVA<br />TIVA →QT |
| `MENSAJE_CONTADOR`        | $0x02$ | Envía información sobre el número de productos creados, el ID, el origen y los objetivos | - numMensaje_objetivo<br />- numMensajes<br />- numMensajes_prod_1<br />- numMensajes_prod_2<br />- id<br />- IDProd | TIVA →QT              |
| `MENSAJE_INICIO`          | $0x03$ |   Inicializa las tareas y establece el número de objetivos   | objetivos                                                    | QT →TIVA              |
| `MENSAJE_TEMPERATURA`     | $0x04$ | Contiene la información sobre las temperaturas medidas en la tarea `temperatura` | - ambiente<br />- soldadura                                  | TIVA →QT              |
| `MENSAJE_ANOMALIAS`       | $0x05$ | Gestiona toda la información que se envía a QT para la muestra en la interfaz de la información de temperatura crítica. | - temp_hazardous<br />-cuenta_atras<br />- war_temp_critico<br />- temp_time_stop | TIVA →QT              |
| `MENSAJE_BLOQUEO`         | $0x06$ | Gestiona la información que se envía a QT para la muestra en la interfaz de la información de bloqueo de productoras | - bloqueado_1<br />- bloqueado_2                             | TIVA →QT              |

