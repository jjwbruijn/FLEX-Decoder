/*
 * FlexDecoder.c
 *
 * Created: 6-7-2017 21:59:50
 * Author : jbruijn
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#define F_CPU 16000000UL
#define UART_BAUD_RATE      115200UL 

#include <util/delay.h>
#include <util/atomic.h>
#include <stdlib.h>

#include "uart.h"
#include "flex.h"
#include "flexprocess.h"
#include "memdebug.h"

char buffer[10];

int main(void){
	
	// start watchdog
	wdt_reset();
	wdt_enable(WDTO_4S);
	
	startFlex();
	
	// setup timer 0 for the second counter, will trigger 125 times / second
	TIMSK0 |= (1<<OCIE0A);
	OCR0A = 125;
	TCCR0B|=(1<<CS02)|(1<<CS00)|(1<<WGM02);
	TCCR0A|=(1<<WGM01)|(1<<WGM00);
	
	// everything else is interrupt driven
	while (1){

	}
}

// triggers 125 times / second
ISR(TIMER0_COMPA_vect){
	sys.subsecond=(sys.subsecond+1)%125;
	sei();
	if(sys.subsecond==0){
		sys.seconds=(sys.seconds+1)%60;
		if(sys.seconds==0){
			sys.minutes=(sys.minutes+1)%60;
			if(sys.minutes==0){
				sys.hour=(sys.hour+1)%24;
			}
		}
		// feed the dog every second
		wdt_reset();
	}
}
