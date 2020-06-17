/*=============================================================================
 * Copyright (c) 2020, José Daniel López <josedlopez11@gmail.com>
 * All rights reserved.
 * License: bsd-3-clause (see LICENSE.txt)
 * Date: 2020/06/16
 * Version: 1
 *===========================================================================*/

/*=====[Inclusions of function dependencies]=================================*/

#include "TPFinalPdC.h"
#include "sapi.h"       	// Librería SAPI
#include "sapi_esp8266.h"	// Librería SAPI ESP8266
#include "sapi_stdio.h"		// Librería SAPI STDIO
#include "ff.h"         	// <= Biblioteca FAT FS
#include "fssdc.h"      	// API de bajo nivel para unidad "SDC:" en FAT FS
#include "env.h"			// Archivo enfironment que contiene las claves de ssid

/*=====[Definition macros of private constants]==============================*/
#define UART_PC        	UART_USB  				// Identificación del UART para la PC.
#define BAUDRATE 		115200            		// Frecuencia a la que se va a comunicar por RS232.
#define FILENAME 		"SDC:/log.txt"  		// Definición de archivo donde se va a guardar la información.
#define WIFI_SSID		ENVWIFISSID     		// SSID Red Wi-Fi
#define WIFI_PASSWORD	ENVWIFIPSWD 			// SSID password
#define WIFI_MAX_DELAY	60000					// Retardo para espera por configuración de Wifi
#define SAMPLE_DELAY	240000					// Retardo para realizar un sampling sin haber una solicitud.
/*=====[Definitions of extern global variables]==============================*/

/*=====[Definitions of public global variables]==============================*/
char buf[100];				// Buff para guardar información en la SD.
static char uartBuff[10];	//
static FATFS fs;           // <-- FatFs work area needed for each volume
static FIL fp;             // <-- File object needed for each open file
static rtc_t rtc = {
   2020,
   6,
   17,
   3,
   17,
   59,
   59
};
bool_t error;
delay_t wifiDelay;
delay_t sampleDelay;
uint16_t muestra = 0;
uint16_t contador = 0;
char data_to_show [1024] = "0";
char label_to_show [1024] = "0";
char HttpWebPageBody [1024];
const char HttpWebPageHeader [] =
		"<!DOCTYPE html>"
		"<html>"
			"<head>"
				"<meta charset='utf-8'>"
				"<meta name='viewport' content='width=device-width, initial-scale=1'>"
				"<title>TPF PdCSE</title>"
				"<link rel='stylesheet' href='https://cdn.jsdelivr.net/npm/bulma@0.9.0/css/bulma.min.css'>"
				"<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/Chart.js/2.9.3/Chart.min.css'>"
				"<script defer src='https://use.fontawesome.com/releases/v5.3.1/js/all.js'></script>"
				"<script defer src='https://cdnjs.cloudflare.com/ajax/libs/Chart.js/2.9.3/Chart.min.js'></script>"
			"</head>"
			"<body>"
				"<section class='section'>"
					"<div class='container'>"
						"<h1 class='title'>"
							"TPFinal Protocolos de Comunicación"
						"</h1>"
						"<p class='subtitle'>"
							"José Daniel López <strong>CESE</strong>!"
						"</p>"
					"</div>"
					"<div class='container'>"
						"<div class='columns'>"
							"<div class='column is-full'>"
								"<canvas id='myChart' style='height:400px; width: content-box;'></canvas>"
							"</div>"
						"</div>"
					"</div>"

				"</section>"
			"</body>"
				"<script>"
					"document.addEventListener('DOMContentLoaded', function(event) {"
						"console.log('DOM fully loaded and parsed');"
						"var ctx = document.getElementById('myChart').getContext('2d');"
						"var myLineChart = new Chart(ctx, {"
							"type: 'line',"
							"data: {"
	;

const char HttpWebPageEnd [] =
									"label: 'Lectura ADC',"
			        		 		"fill: false,"
			        		 		"backgroundColor: 'rgba(63,127,191,1)',"
			        		 		"borderColor: 'rgba(63,127,191,1)',"
						 	 	 "}]"
							"},"
							"options: {"
								"responsive: true,"
								"maintainAspectRatio: false,"
								"scales: {"
									"yAxes: [{"
										"ticks: {"
											"beginAtZero:true"
										"}"
									"}]"
								"}"
							"}"
						"});"
					"});"
				"</script>"
	"</html>"
	;

/*=====[Main function, program entry point after power on or reset]==========*/
int main( void )
{
	// ----- Setup -----------------------------------
	boardInit();                           // Inicializamos la tarjeta.
	uartConfig(UART_PC, BAUDRATE );        // Configuramos el UART para comunicación con el PC
	adcConfig( ADC_ENABLE );               // Habilitamos el ADC
	spiConfig( SPI0 );                     // Habilitamos el SPI para comunicarnos con la SD

	uartWriteString(UART_PC, "UART, ADC y SPI configurados.\r\n" );

	FSSDC_InitSPI ();                         // Inicializamos el SPI para escribir en la SD.
	if( f_mount( &fs, "SDC:", 0 ) != FR_OK ){ // Confirmamos que este disponible la SD, de lo contrario enviamos mensaje de que no esta disponible
	  while (1) {
		 gpioToggle( LEDR );
		 printf("SD no disponible\n");
		 delay(1000);
	  }
	}
	gpioWrite(LED1,1);						// Led indicador que la SD está OK.

	printf("Inicializamos RTC.\r\n");
	rtcInit(); 								// Iniciamos RTC
	rtcWrite( &rtc ); 						// Establecemos fecha y hora.
	delay(2000);							// Realizamos un delay de 2 segundos para normalizar.
	printf("RTC listo.\r\n");
	gpioWrite(LED2,1);						// Señalamos con un led que el RTc está ok


	uartWriteString( UART_USB, "Configurando el ESP8266.\r\n" );
	error = FALSE;								// Establecemos en Falso la variable de error.
	delayConfig(&wifiDelay, WIFI_MAX_DELAY);	// Configura un delay para salir de la configuracion en caso de error.

	while (!esp8266ConfigHttpServer(WIFI_SSID, WIFI_PASSWORD) && !error) {
	 if (delayRead(&wifiDelay)) {
		error = TRUE;
	 }
	}

	// Avisa al usuario como salio la configuracion
	if (!error) {
	 stdioPrintf(UART_USB, "\n\rServidor HTTP configurado. Puedes ingresar a la IP: %s para consultar los datos muestreados.", esp8266GetIpAddress());
	 // Enciende LEDG indicando que el modulo esta configurado.
	 gpioWrite(LED3, TRUE);
	} else {
	 stdioPrintf(UART_USB, "\n\rError al configurar servidor HTTP.");
	 // Enciende LEDR indicando que el modulo esta en error.
	 gpioWrite(LEDR, TRUE);
	}

	delayConfig(&sampleDelay, SAMPLE_DELAY);			// Configuramos el delay de muestreo para realizar muestra cuando haya una solicitud http o cada 4 minutos.
   // ----- Repeat for ever -------------------------
	while( true ) {
		if (delayRead(&sampleDelay) || esp8266ReadHttpServer()) {
			if (contador > 10){
				stdioSprintf(label_to_show,"0",label_to_show,rtc.hour,rtc.min);
				stdioSprintf(data_to_show,"0",data_to_show,muestra);
				contador = 0;
			}
			muestra = adcRead( CH1 );
			rtcRead( &rtc );
			printf("Se realiza una muestra del ADC %02d \r\n",muestra);
			if( f_open( &fp, "SDC:/log.txt", FA_WRITE | FA_OPEN_APPEND ) == FR_OK ){
				int nbytes;
				int n = sprintf( buf, "{'sample_time':'%04d-%02d-%02d %02d:%02d:%02d','sample_value':%02d},",
						rtc.year,
						rtc.month,
						rtc.mday,
						rtc.hour,
						rtc.min,
						rtc.sec,
						muestra
				);
				f_write( &fp, buf, n, &nbytes );
				f_close(&fp);

				if( nbytes == n ){
					printf("Se escribe en la SD correctamente.\r\n");
					gpioWrite( LEDG, ON );


					stdioSprintf(label_to_show,"%s,'%02d:%02d'",label_to_show,rtc.hour,rtc.min);
					stdioSprintf(data_to_show,"%s,%02d",data_to_show,muestra);
					stdioSprintf(HttpWebPageBody,"labels: [%s] , datasets: [{ data: [%s] ,",label_to_show,data_to_show);
					contador = contador + 1;
					error = FALSE;
					delayConfig(&wifiDelay, WIFI_MAX_DELAY);

					// Mientras no termine el envio o mientras no pase el tiempo maximo, ejecuta el envio.
					while (!esp8266WriteHttpServer(HttpWebPageHeader, HttpWebPageBody, HttpWebPageEnd) && !error) {
						if (delayRead(&wifiDelay)) {
							error = TRUE;
						}
					}

					// Avisa al usuario como fue el envio
					if (!error) {
						stdioPrintf(UART_USB, "Peticion respondida al cliente HTTP %d.\r\n", esp8266GetConnectionId());
						gpioWrite( LEDG, OFF );
					} else {
						stdioPrintf(UART_USB, "Peticion no respondida al cliente HTTP %d.\r\n", esp8266GetConnectionId());
						gpioToggle(LEDR);
					}

				} else {
				   gpioWrite( LEDR, ON );
				   printf("Escritura incompleta, se escribieron %d bytes.\r\n", nbytes);
				}
			} else{
				printf("Error abriendo el archivo.\r\n");
				gpioWrite( LEDR, ON );
			}

		}
   }

   // YOU NEVER REACH HERE, because this program runs directly or on a
   // microcontroller and is not called by any Operating System, as in the 
   // case of a PC program.
   return 0;
}
