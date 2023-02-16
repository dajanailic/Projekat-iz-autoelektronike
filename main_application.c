/* Standard includes. */
#include <stdio.h>
#include <conio.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"


/* Hardware simulator utility functions */
#include "HW_access.h"

/* SERIAL SIMULATOR CHANNEL TO USE */
#define COM_CH (0)
#define COM_CH1 (1)

	/* TASK PRIORITIES */
#define TASK_ALARM_PRI				( tskIDLE_PRIORITY + 1 ) 
#define	TASK_SERIAL_SEND_PRI		( tskIDLE_PRIORITY + 6 )
#define TASK_SERIAl_REC_PRI			( tskIDLE_PRIORITY + 8 )
#define	SERVICE_TASK_PRI		    ( tskIDLE_PRIORITY + 2 )
#define TASK_PC_COMMAND				( tskIDLE_PRIORITY + 3 )
#define TASK_SENZOR_PRI				( tskIDLE_PRIORITY + 4 )
#define TASK_SEG7_Task				( tskIDLE_PRIORITY + 5 )
#define TASK_PC_SERIAL_REC			( tskIDLE_PRIORITY + 7 )


	/* TASKS: FORWARD DECLARATIONS */
void led_bar_tsk(void* pvParameters);
void SerialSend_Task(void* pvParameters);
void SerialReceive_Task(void* pvParameters);
void PC_SerialReceive_Task(void* pvParameters);
void Prijem_sa_senzora(void* pvParameters);
void Seg7_ispis_task(void* pvParameters);
void AlarmTask(void* pvParameters);
void PC_command(void* pvParameters);


/* TRASNMISSION DATA - CONSTANT IN THIS APPLICATION */
const char trigger[] = "XYZ";
unsigned volatile t_point;

/* RECEPTION DATA BUFFER */
#define R_BUF_SIZE (32)
uint8_t r_buffer[R_BUF_SIZE];
unsigned volatile r_point;

/* 7-SEG NUMBER DATABASE - ALL HEX DIGITS */
static const char hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
								0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };

/*PORUKE*/
/*poruke stanja*/
#define MONITOR 'm'
#define DRIVE   'd'
#define SPEED   's'
#define OFF     'o'

/*led poruke*/
#define ALARM_ON 0xff
#define LED_ON   0x01
#define LED_OFF  0x00

/*BUTTON MASKS*/
#define BTN1 0x04
#define BTN2 0x02
#define BTN3 0x01
#define NO_BTN 0x00

/*kriticne vrednosti*/
#define MAX_TEMP_rashladne_tecnosti 110u
#define MAX_obrtaji		 6000u

/*podaci sa senzora*/
typedef struct vrednost_senzora {/*struct for holding sensor values*/
	uint8_t temp_vazduha;
	uint8_t temp_rashladne_tecnosti;
	uint16_t obrtaji;
	uint8_t temp_usisna_grana;
	uint8_t pedala_gasa;
}vrednost_senzora;

typedef struct komande {
	uint8_t rec[7];
	uint8_t duzina;
}komande;

uint8_t Seg_data[10] = {0};

/* GLOBAL OS-HANDLES */
SemaphoreHandle_t LED_INT_BinarySemaphore;
SemaphoreHandle_t TBE_BinarySemaphore;
SemaphoreHandle_t RXC_BinarySemaphore;
SemaphoreHandle_t RXC_PCSemaphore;
SemaphoreHandle_t AlarmStateSem;
TimerHandle_t per_TimerHandle;
QueueHandle_t SensorQueue;
QueueHandle_t MessageQueue;
QueueHandle_t Seg7Queue;
QueueHandle_t LedQueue;
QueueHandle_t PCcommand;

void Seg7Data(vrednost_senzora SensTemp, uint8_t podatak);

/* OPC - ON INPUT CHANGE - INTERRUPT HANDLER */
static uint32_t OnLED_ChangeInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	xSemaphoreGiveFromISR(LED_INT_BinarySemaphore, &xHigherPTW);

	portYIELD_FROM_ISR(xHigherPTW);
}

/* TBE - TRANSMISSION BUFFER EMPTY - INTERRUPT HANDLER */
static uint32_t prvProcessTBEInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	xSemaphoreGiveFromISR(TBE_BinarySemaphore, &xHigherPTW);

	portYIELD_FROM_ISR(xHigherPTW);
}


/* RXC - RECEPTION COMPLETE - INTERRUPT HANDLER */
static uint32_t prvProcessRXCInterrupt(void)
{
	BaseType_t xHigherPTW = pdFALSE;

	if (get_RXC_status(0) != 0)
		xSemaphoreGiveFromISR(RXC_BinarySemaphore, &xHigherPTW);

	if (get_RXC_status(1) != 0)
		xSemaphoreGiveFromISR(RXC_PCSemaphore, &xHigherPTW);

	portYIELD_FROM_ISR(xHigherPTW);
}

/* PERIODIC TIMER CALLBACK */
static void TimerCallback(TimerHandle_t xTimer)
{
	xSemaphoreGive(TBE_BinarySemaphore);
	xSemaphoreGive(AlarmStateSem);
}

/* MAIN - SYSTEM STARTUP POINT */
void main_demo(void)
{
	init_7seg_comm();
	init_LED_comm();
	init_serial_uplink(COM_CH);// inicijalizacija serijske TX na kanalu 0
	init_serial_uplink(COM_CH1);
	init_serial_downlink(COM_CH);// inicijalizacija serijske TX na kanalu 0
	init_serial_downlink(COM_CH1);

	/* QUEUES  */
	SensorQueue = xQueueCreate(1u, sizeof(vrednost_senzora));
	MessageQueue = xQueueCreate(5u, sizeof(uint8_t));
	Seg7Queue = xQueueCreate(10u, sizeof(uint8_t));
	LedQueue = xQueueCreate(1u, sizeof(uint8_t));
	PCcommand = xQueueCreate(1u, sizeof(komande));

	/* ON INPUT CHANGE INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_OIC, OnLED_ChangeInterrupt);

	/* Create LED interrapt semaphore */
	LED_INT_BinarySemaphore = xSemaphoreCreateBinary();

	/* create a timer task */
	per_TimerHandle = xTimerCreate("Timer", pdMS_TO_TICKS(500), pdTRUE, NULL, TimerCallback);
	xTimerStart(per_TimerHandle, 0);


	/* SERIAL TRANSMITTER TASK */
	xTaskCreate(SerialSend_Task, "STx", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAL_SEND_PRI, NULL);

	/* SERIAL RECEIVER TASK */
	xTaskCreate(SerialReceive_Task, "SRx", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI, NULL);
	r_point = 0;

	/*Create alarm task*/
	xTaskCreate(AlarmTask, "ALRM", configMINIMAL_STACK_SIZE, NULL, TASK_ALARM_PRI, NULL);

	/*Create sensor data handler task*/
	xTaskCreate(Prijem_sa_senzora, "Pss", configMINIMAL_STACK_SIZE, NULL, TASK_SENZOR_PRI, NULL);

	/*Create 7 segment driver task*/
	xTaskCreate(Seg7_ispis_task, "Se7", configMINIMAL_STACK_SIZE, NULL, TASK_SEG7_Task, NULL);

	xTaskCreate(PC_SerialReceive_Task, "PCRx", configMINIMAL_STACK_SIZE, NULL, TASK_PC_SERIAL_REC, NULL);

	/*Create a task that handles pc command*/
	xTaskCreate(PC_command, "PCCx", configMINIMAL_STACK_SIZE, NULL, TASK_PC_COMMAND, NULL);

	/* Create TBE semaphore - serial transmit comm */
	TBE_BinarySemaphore = xSemaphoreCreateBinary();

	/* Create TBE semaphore - serial transmit comm */
	RXC_BinarySemaphore = xSemaphoreCreateBinary();
	RXC_PCSemaphore = xSemaphoreCreateBinary();

	/*Create State Semaphores*/
	AlarmStateSem = xSemaphoreCreateBinary();

	/* SERIAL TRANSMISSION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_TBE, prvProcessTBEInterrupt);

	/* SERIAL RECEPTION INTERRUPT HANDLER */
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, prvProcessRXCInterrupt);

	/* create a led bar TASK */
	xTaskCreate(led_bar_tsk, "ST", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);

	vTaskStartScheduler();

	while (1);
}



void Prijem_sa_senzora(void* pvParameters)
{

	vrednost_senzora SensTemp; /*bafer*/
	uint8_t led = LED_OFF;/*inicijalno su ledovke iskljucene*/
	uint8_t podatak = OFF;/*sistem u pocetku iskljucen*/
	while (1)
	{
		xQueueReceive(SensorQueue, &SensTemp, portMAX_DELAY);/*task ceka u blokiranom stanju dok ne dodje podatak sa queque*/
		/*provera da li je stigao novi mod, ako nije koristi se stari*/
		if (uxQueueMessagesWaiting(MessageQueue)) {
			xQueueReceive(MessageQueue, &podatak, portMAX_DELAY);
		}

		Seg7Data(SensTemp, podatak);/*mod i vrednosti sa senzora za displej*/
		/*Provera kako radi led displej*/
		if ((MONITOR == podatak) || (DRIVE == podatak) || (SPEED == podatak)) {/*Ako smo u nekom radnom modu*/
			if (MAX_TEMP_rashladne_tecnosti < SensTemp.temp_rashladne_tecnosti)
			{/*provera da li je temperatura veca od granice*/
				led = ALARM_ON;
			}
			else if (MAX_obrtaji < SensTemp.obrtaji)
			{/*provera da li je broj obrtaja veci od granice*/
				led = ALARM_ON;
			}
			else/*ako je sve u redu ukljuciti led*/
			{
				led = LED_ON;
			}
		}
		else/*Ako je pogresna poruka stigla, iskljuciti*/
		{
			led = LED_OFF;
		}
		xQueueSend(LedQueue, &led, portMAX_DELAY);/*poslati podatke u task za alarm*/
	}
}

/*Ispisivanje vrednosti na 7seg displej*/
void Seg7_ispis_task(void* pvParameters)
{
	uint8_t Seg_data;
	int i;
	while (1)
	{
		for (i = 0; i < 10; i++) {
			xQueueReceive(Seg7Queue, &Seg_data, portMAX_DELAY);
			select_7seg_digit(i);
			set_7seg_digit(hexnum[Seg_data]);
		}
	}


}

void AlarmTask(void* pvParameters)
{
	uint8_t led = LED_OFF;
	uint8_t temp_led;
	while (1)
	{
		xSemaphoreTake(AlarmStateSem, portMAX_DELAY);/*potrebno je da bude ovdje 200ms za blinkanje*/
		temp_led = led;/*zapamtiti staru vrednost zbog poredjenja*/
		xQueueReceive(LedQueue, &led, portMAX_DELAY);/*ucitavanje novog podatka*/
		if ((temp_led == led) && (ALARM_ON == led))
		{/*ugasiti sve ledovke ako je alarm ukljucen*/
			led = ALARM_ON;

			
				set_LED_BAR(1, led);
				vTaskDelay(pdMS_TO_TICKS(200));
				set_LED_BAR(1, LED_OFF);
				vTaskDelay(pdMS_TO_TICKS(200));
			
		}
		else
		{
			set_LED_BAR(1, led);
		}
	}
}
/*cuvanje vrednosti sa ulaznih ledovki*/
void led_bar_tsk(void* pvParameters)
{
	unsigned i;
	uint8_t d;
	uint8_t mode;
	while (1)
	{
		/*blokiranje taska*/
		/*iscitati vrednosti poslati poruku*/
		xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);
		get_LED_BAR(0, &d);
		switch (d)
		{
		case BTN1:/*prvo dugme je za MONITOR*/
			mode = MONITOR;
			break;
		case BTN2:/*Drugo dugme je za DRIVE*/
			mode = DRIVE;
			break;
		case BTN3:/*Trece dugme je za SPEED*/
			mode = SPEED;
			break;
		case NO_BTN:
			mode = OFF;
			break;
		default:/*random dugme gasi sistem*/
			mode = OFF;
			break;
		}
		xQueueSend(MessageQueue, &mode, portMAX_DELAY);
	}
}
void PC_command(void* pvParameters)
{
	uint8_t mode = OFF;
	komande command;
	while (1)
	{
		xQueueReceive(PCcommand, &command, portMAX_DELAY);
		if (0u != command.duzina) {
			if (0u == strncmp(command.rec, "MONITOR", command.duzina))
			{
				mode = MONITOR;
			}
			else if (0u == strncmp(command.rec, "DRIVE", command.duzina))
			{
				mode = DRIVE;
			}
			else if (0u == strncmp(command.rec, "SPEED", command.duzina))
			{
				mode = SPEED;
			}
			else
			{/*Ako je nesto drugo poslato iskljuciti sistem*/
				mode = OFF;
			}

			xQueueSend(MessageQueue, &mode, 0u);

		}

	}
}



/*Task za primanje komnadi sa pc-a*/
void PC_SerialReceive_Task(void* pvParameters)
{
	uint8_t cc;
	komande temp;
	uint8_t pos = 0;
	uint8_t podatak = OFF;
	while (1)
	{
		xSemaphoreTake(RXC_PCSemaphore, portMAX_DELAY);/*blokiraj task dok se ne primi karakter*/
		get_serial_character(COM_CH1, &cc);
		if (0x0d == cc)
		{
			xQueueOverwrite(PCcommand, &temp);/*salje komandu*/
			pos = 0u;/*restartuje poziciju za sledecu komadnu*/
		}
		else/*ako nije kraj komande, smesti  u bafer*/
		{
			if (6u >= pos) {
				temp.rec[pos] = cc;/*dodati karakter*/
				temp.duzina = pos + 1u;
				pos++;
			}
			else
			{/*ako rec ima vise od 7 karaktera*/
				xQueueSend(MessageQueue, &podatak, 0u);
				temp.duzina = 0u;
			}

		}
	}
}

void SerialSend_Task(void* pvParameters)
{
	t_point = 0;
	while (1)
	{
		if (t_point > (sizeof(trigger) - 1))/*ako je poslan triger, restartuje za novi*/
		{
			t_point = 0;
		}
		if (0u == t_point)/*ako je prvi karakter od trigera blokiraj task*/
		{
			vTaskDelay(pdMS_TO_TICKS(200)); // kada se koristi vremenski delay }
			//xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);/*blokiraj task na 100ms*/
		}
		send_serial_character(COM_CH, trigger[t_point++]); /*slanje trigera*/
		vTaskDelay(pdMS_TO_TICKS(200)); // kada se koristi vremenski delay }
	//xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);/*blokiraj task dok se ne isprani bafer*/
	}
} 

void SerialReceive_Task(void* pvParameters)
{
	uint8_t r_point = 0;
	uint8_t r_buffer[7] = {0};
	uint8_t cc = 0;
	
	
	uint8_t podatak = OFF;
	static uint8_t loca = 0; 
	vrednost_senzora SensTemp;
	while (1)
	{
		xSemaphoreTake(RXC_BinarySemaphore, portMAX_DELAY);/*blokiraj task dok ne primimo karakter*/
		get_serial_character(COM_CH, &cc);

		if (cc == 0xff) // oznaciti kraj poruke i ako je kraj, preko reda poslati informacije o poruci i restartovati ovaj taks
		{

			SensTemp.temp_vazduha = r_buffer[2];
			SensTemp.temp_rashladne_tecnosti = r_buffer[3];
			SensTemp.obrtaji = r_buffer[4];
			SensTemp.temp_usisna_grana = r_buffer[5];
			SensTemp.pedala_gasa = r_buffer[6];
			r_point = 0;
			xQueueSend(SensorQueue, &SensTemp, 0U);
		}
		else if (r_point < R_BUF_SIZE)// pamti karaktere izmedju 0 i FF
		{
			r_buffer[r_point++] = cc;
		}
		else
		{
			xQueueSend(MessageQueue, &podatak, 0U);
		}
	}
}


void Seg7Data(vrednost_senzora SensTemp, uint8_t podatak)
{
	uint8_t Seg_data[10] ;
	int i;
	switch (podatak)
	{
	case MONITOR:
		Seg_data[0] = 1;
		for (i = 0; i <= 3; i++) {
			Seg_data[9 - i] = SensTemp.temp_rashladne_tecnosti % 10;
			SensTemp.temp_rashladne_tecnosti /= 10;
		}
		Seg_data[5] = 0;
		for (i = 0; i <= 3; i++) {
			Seg_data[4 - i] = SensTemp.temp_vazduha % 10;
			SensTemp.temp_vazduha /= 10;
		}
		break;
	case DRIVE:
		Seg_data[0] = 2;
		if (9999u < SensTemp.obrtaji)
		{
			SensTemp.obrtaji = 9999u;
		}
		for (i = 0; i <= 3; i++) {
			Seg_data[9 - i] = SensTemp.obrtaji % 10;
			SensTemp.obrtaji /= 10;
		}
		Seg_data[5] = 0;
		for (i = 0; i <= 3; i++) {
			Seg_data[4 - i] = SensTemp.temp_usisna_grana % 10;
			SensTemp.temp_usisna_grana /= 10;
		}
		break;
	case SPEED:
		Seg_data[0] = 3;
		if (9999u < SensTemp.obrtaji)
		{
			SensTemp.obrtaji = 9999u;
		}
		for (i = 0; i <= 3; i++) {
			Seg_data[9 - i] = SensTemp.obrtaji % 10;
			SensTemp.obrtaji /= 10;
		}
		Seg_data[5] = 0;
		for (i = 0; i <= 3; i++) {
			Seg_data[4 - i] = SensTemp.pedala_gasa % 10;
			SensTemp.pedala_gasa /= 10;
		}
		break;
	default:

		break;
	}

	for (i = 0; i <= 9; i++)
	{
		xQueueSend(Seg7Queue, &Seg_data[i], portMAX_DELAY);
	}
}


void vApplicationIdleHook(void) {
}

