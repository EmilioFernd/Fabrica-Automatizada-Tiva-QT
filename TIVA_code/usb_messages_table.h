/*
 * Listado de los tipos de mensajes empleados en la aplicaci�n, as� como definiciones de sus par�metros.
 * (TODO) Incluir aqu� las respuestas a los mensajes
*/
#ifndef __USB_MESSAGES_TABLE_H
#define __USB_MESSAGES_TABLE_H

#include<stdint.h>

//Codigos de los mensajes. EL estudiante deberá definir los c�digos para los mensajes que vaya
// a crear y usar. Estos deberan ser compatibles con los usados en la parte Qt

typedef enum {
    MENSAJE_NO_IMPLEMENTADO,
    MENSAJE_PING,
    MENSAJE_CONTADOR,
    MENSAJE_INICIO,
    MENSAJE_TEMPERATURA,
    MENSAJE_ANOMALIAS,
    //etc, etc...
} messageTypes;

//Estructuras relacionadas con los parametros de los mensajes. El estuadiante debera crear las
// estructuras adecuadas a los mensajes usados, y asegurarse de su compatibilidad con el extremo Qt

//#pragma pack(1)   //Con esto consigo que el alineamiento de las estructuras en memoria del PC (32 bits) no tenga relleno.
//Con lo de abajo consigo que el alineamiento de las estructuras en memoria del microcontrolador no tenga relleno

typedef struct {
    uint32_t id;
    uint8_t IDProd;
} INFO_COLA;

typedef struct {
    uint8_t IDProd;
    int8_t retardo;
    //uint8_t ui8Pins;
} PARAM_PRODUCTORA;

typedef struct {
    uint32_t R;
    uint32_t G;
    uint32_t B;
    float intensity;
} PARAM_RGBDATA;


#define PACKED __attribute__ ((packed))

typedef struct {
    uint8_t objetivos;
}PACKED PARAM_MENSAJE_INICIO;

typedef struct {
    uint8_t message;
} PACKED PARAM_MENSAJE_NO_IMPLEMENTADO;

typedef struct {
    uint8_t numMensaje_objetivo;
    uint16_t numMensajes;
    uint16_t numMensajes_prod_1;
    uint16_t numMensajes_prod_2;
    uint32_t id;
    uint8_t IDProd;
} PACKED PARAM_MENSAJE_CONTADOR;

typedef struct {
    uint32_t ambiente;
    uint32_t soldadura;
} PACKED PARAM_MENSAJE_TEMPERATURA;

typedef struct {
    uint8_t bloqueado_1;
    uint8_t bloqueado_2;
    uint8_t temp_hazardous;
    uint8_t cuenta_atras;
    uint8_t war_temp_critico;
} PACKED PARAM_MENSAJE_ANOMALIAS;
//#pragma pack()    //...Pero solo para los mensajes que voy a intercambiar, no para el resto





#endif
