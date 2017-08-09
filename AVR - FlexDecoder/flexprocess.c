/*
 * flexprocess.c
 *
 * Created: 11-7-2017 22:51:23
 *  Author: jbruijn
 */ 

#include <avr/io.h>
#include <stdlib.h>
#include <util/atomic.h>
#include <string.h>


#define F_CPU 16000000UL
#define UART_BAUD_RATE      115200UL
#include "uart.h"

#define SERDEBUG

#include "flex.h"
#include "flexprocess.h"
#include "memdebug.h"

struct mapping* mapping[MAX_MAPPINGS];

struct message* messages[MAX_MESSAGES];

char buffer[15];

volatile uint8_t previousframe = 0xFF;

struct vector decodeVector(uint32_t vword, uint32_t address){
	struct vector vect;
	
	vect.type = bitswitch((uint8_t)(vword>>20))&0x07;
	if(!vword){
		vect.type=VECT_NULL;
		return vect;
	}
	
	vect.address = decodeAddress(address);
	switch(vect.type){
		case VECT_ALPHA:
		case VECT_HEX:
		case VECT_SECURE:
			vword>>=10;
			vect.length = bitswitch((uint8_t)vword)&0x7F;
			vword>>=7;
			vect.start = bitswitch((uint8_t)vword)&0x7F;
			#ifdef SERDEBUG
				uart_puts_P("| MESSAGE location word: ");itoa(vect.start, buffer, 10);uart_puts(buffer);
				uart_puts_P(" length:");itoa(vect.length, buffer, 10);uart_puts(buffer);
				uart_puts_P("\n\r");
			#endif
			break;
		case VECT_INSTRUCTION:
			vword>>=7;
			vect.tempaddr = bitswitch((uint8_t)vword)&0x0F;
			vword>>=7;
			vect.tempframe = bitswitch((uint8_t)vword)&0x7F;
			break;
	}
	return vect;
}

uint8_t getValidity(struct frame* frame, uint8_t word){
	// Provides a specific word from the given frame
	return (frame->block[word/8]->check)&(1<<(word%8));	
}

void processBIW(struct frame* frame){
	// Decodes the block information word, to see what data is stored where
	uint32_t biwword = frame->block[0]->word[0];
	biwword>>=6;
	frame->biw.collapse = bitswitch((uint8_t)biwword)&0x07;
	biwword>>=2;
	frame->biw.carryon = bitswitch((uint8_t)biwword)&0x03;
	biwword>>=6;
	frame->biw.vectorstart = bitswitch((uint8_t)biwword)&0x3F;
	biwword>>=2;
	frame->biw.endofblockinfo = bitswitch((uint8_t)biwword)&0x03;
	frame->biw.addressstart = frame->biw.endofblockinfo+1;
	biwword>>=4;
	frame->biw.priority = bitswitch((uint8_t)biwword)&0x0F;
}

void processBIW2(uint32_t biwword){
	// other type of block information word, with 4 different subtypes. Generally to provide the time. This function stores this information in a 'system' structure,
	// but no actual RTC stuff happens. I was too lazy; Flex-time is currently off by 15 seconds anyway, so it's of no real use. Besides that, the cycle and frame
	// number of all messages are provided, which gives you a 2-second time resolution. Good enough.
	uint32_t copy = biwword;
	copy>>=20;
	copy = bitswitch((uint8_t)copy)&0x07;
	switch(copy){
		case 0x00: //local id
			biwword>>=17;
			sys.timezone = bitswitch((uint8_t)biwword)&0x1F;
			#ifdef SERDEBUG
				uart_puts_P("| LOCAL ID SET, TZ=");itoa(sys.timezone, buffer, 10);uart_puts(buffer);
				uart_puts_P("\n\r");
			#endif
			break;
		case 0x01: // MDY
			biwword>>=7;
			sys.month = bitswitch((uint8_t)biwword)&0x0F;
			biwword>>=5;
			sys.day = bitswitch((uint8_t)biwword)&0x1F;
			biwword>>=5;
			sys.year = 1994+(bitswitch((uint8_t)biwword)&0x1F);
			#ifdef SERDEBUG
				uart_puts_P("| DATE SET: ");itoa(sys.day, buffer, 10);uart_puts(buffer);
				uart_puts_P("/");itoa(sys.month, buffer, 10);uart_puts(buffer);
				uart_puts_P("/");itoa(sys.year, buffer, 10);uart_puts(buffer);
				uart_puts_P("\n\r");
			#endif
			break;
		case 0x02: // HMS
			biwword>>=6;
			sys.seconds = bitswitch((uint8_t)biwword)&0x07;
			sys.seconds = ((sys.seconds*7)+(sys.seconds>>1));
			biwword>>=6;
			sys.minutes = bitswitch((uint8_t)biwword)&0x3F;
			biwword>>=5;
			sys.hour = bitswitch((uint8_t)biwword)&0x1F;
			#ifdef SERDEBUG
				uart_puts_P("| TIME SET: ");itoa(sys.hour, buffer, 10);uart_puts(buffer);
				uart_puts_P(":");itoa(sys.minutes, buffer, 10);uart_puts(buffer);
				uart_puts_P(":");itoa(sys.seconds, buffer, 10);uart_puts(buffer);
				uart_puts_P("\n\r");
			#endif
			break;
		case 0x03: // Spare / offset
			#ifdef SERDEBUG
				uart_puts_P("| SPARE/OFFSET SET");
				uart_puts_P("\n\r");
			#endif
			break;
	}
}

void initProcessor(void){
	// initializes some stuff for the flex frame processor, such as the parking table for long messages, and addressfield mapping table
	procmutex = 0;
	uint8_t count;
	for(count = 0;count<MAX_MAPPINGS;count++){
		mapping[count]=0;
	}
	
	for(count=0;count<MAX_MESSAGES;count++){
		messages[count]=0;
	}
}

void cleanUpMessage(struct message* msg){
	// this recursively deletes everything that might be associated with a message
		
	// if the message was stored because it was fragmented, clear the reference in the table, 
	if(msg->location!=NO_LOC_ASSIGNED){
		messages[msg->location]=0;
	}
	
	// remove addresslist, message data and finally the message struct itself
	ATOMIC_BLOCK(ATOMIC_FORCEON){
		if(msg->addresslist.addresspointer)free(msg->addresslist.addresspointer);
		if(msg->messagep)free(msg->messagep);
		if(msg)free(msg);
	}
}

struct message* addMessage(){
	// allocates space for a message, and sets relevant values
	struct message* msg;
	ATOMIC_BLOCK(ATOMIC_FORCEON){
		msg = calloc(1,sizeof(struct message));
	}
	if(!msg){
		return NULL;
	}
	msg->messagep = 0;
	msg->addresslist.addresspointer = 0;
	msg->addresslist.addresscount = 0;
	msg->messagelength = 0;	
	msg->iscomplete = 0;
	msg->timeout = LONG_MSG_TTL;
	msg->sigtemp = 0;
	msg->signature = 0;
	msg->location = NO_LOC_ASSIGNED;
	return msg;
}

void clearMappings(uint8_t curframe){
	// removes mappings for the current frame
	uint8_t count;
	for(count=0;count<MAX_MAPPINGS;count++){
		if(mapping[count]){
			if(mapping[count]->frame==curframe){
				ATOMIC_BLOCK(ATOMIC_FORCEON){
					if(mapping[count]->addressp)free(mapping[count]->addressp);
					free(mapping[count]);
				}
				mapping[count]=0;
			}
		}
	}
}

uint8_t addMapping(uint8_t frame, uint8_t tempaddress, uint32_t address){
	uint8_t count;
	uint32_t* tempp;
	for(count=0;count<MAX_MAPPINGS;count++){
		if(mapping[count]){
			if((mapping[count]->frame==frame)&&(mapping[count]->tempaddress==tempaddress)){
				mapping[count]->addresscount++;
				
				// okay so this is a little convoluted, but here goes:
				tempp = mapping[count]->addressp; // save the pointer (temporarily
				ATOMIC_BLOCK(ATOMIC_FORCEON){
					// attempt to reallocated the block
					mapping[count]->addressp = realloc(mapping[count]->addressp, sizeof(uint32_t)*mapping[count]->addresscount);
				}
				// check if the reallocation succeeded
				if(mapping[count]->addressp==NULL){
						// if it didnt, restore the old count, pointer and return immediately
						mapping[count]->addresscount--;
						mapping[count]->addressp = tempp;
						return 0;
				} else {
					// it succeeded, add the mapping
					mapping[count]->addressp[(mapping[count]->addresscount)-1]=address;
				}
				
				#ifdef SERDEBUG
					uart_puts_P("| RIC: ");ultoa(address-32768, buffer, 10);uart_puts(buffer);
					uart_puts_P(" will join temporary address 0x");ultoa(tempaddress+0x01F7800, buffer, 16);uart_puts(buffer);
					uart_puts_P(" for frame ");itoa(frame, buffer, 10);uart_puts(buffer);
					uart_puts_P("\n\r");
				#endif
				return 1;
			}
		}
	}
	for(count=0;count<MAX_MAPPINGS;count++){
		if(!mapping[count]){
			ATOMIC_BLOCK(ATOMIC_FORCEON){
				mapping[count]=calloc(1,sizeof(struct mapping));
			}
			if(mapping[count]==NULL){
				return 0;
			}
			mapping[count]->tempaddress=tempaddress;
			mapping[count]->frame=frame;
			mapping[count]->addresscount=1;
			ATOMIC_BLOCK(ATOMIC_FORCEON){
				mapping[count]->addressp = calloc(1, sizeof(uint32_t));
			}
			if(mapping[count]->addressp==NULL){
				return 0;
			}			
			#ifdef SERDEBUG
				uart_puts_P("| New mapping for temporary address 0x");ultoa(tempaddress+0x01F7800, buffer, 16);uart_puts(buffer);
				uart_puts_P(" frame ");itoa(frame, buffer, 10);uart_puts(buffer);
				uart_puts_P("\n\r");
				uart_puts_P("| RIC: ");ultoa(address-32768, buffer, 10);uart_puts(buffer);
				uart_puts_P(" will join temporary address 0x");ultoa(tempaddress+0x01F7800, buffer, 16);uart_puts(buffer);
				uart_puts_P(" for frame ");itoa(frame, buffer, 10);uart_puts(buffer);
				uart_puts_P("\n\r");
			#endif
			mapping[count]->addressp[0]=address;
			return 1;
		}
	}
	return 0;
}

void addMappingsToMessage(uint32_t address,uint8_t frame, struct message* msg){
	// this function takes a temporary address as argument, and finds all associated normal addresses. These addresses are then added to the message
	uint8_t count;
	uint32_t* tempp;
	for(count=0;count<MAX_MAPPINGS;count++){
		if(mapping[count]){
			if(mapping[count]->frame==frame){
				if(mapping[count]->tempaddress==(uint8_t)address){
					tempp = msg->addresslist.addresspointer;
					ATOMIC_BLOCK(ATOMIC_FORCEON){
						// protected
						msg->addresslist.addresspointer = realloc(msg->addresslist.addresspointer, (msg->addresslist.addresscount+mapping[count]->addresscount)*sizeof(uint32_t));
					}
					if(msg->addresslist.addresspointer==NULL){
						msg->addresslist.addresspointer = tempp;
					} else {
						memcpy(msg->addresslist.addresspointer+(msg->addresslist.addresscount*sizeof(uint32_t)), mapping[count]->addressp, mapping[count]->addresscount*sizeof(uint32_t));
						msg->addresslist.addresscount+=mapping[count]->addresscount;
					}
				}
			}
		}
	}
}

void addAddressToMessage(uint32_t address, uint8_t frame, struct message* msg){
	uint32_t* tempp;
	// adds an address to a message, or adds all addresses that were assigned to a temporary address. You will now all refer to me by the name... Betty
	if((address>>4)==0x1F780){
		addMappingsToMessage(address,frame,msg);
	} else {
		msg->addresslist.addresscount+=1;
		tempp = msg->addresslist.addresspointer;
		ATOMIC_BLOCK(ATOMIC_FORCEON){
			// protected
			msg->addresslist.addresspointer = realloc(msg->addresslist.addresspointer,(msg->addresslist.addresscount)*sizeof(uint32_t));
		}
		if(!msg->addresslist.addresspointer){
			msg->addresslist.addresspointer = tempp;
			return;
		} else {
			msg->addresslist.addresspointer[(msg->addresslist.addresscount)-1]=address;
		}
	}
}

uint32_t decodeAddress(uint32_t addressword){
	// this basically flips an entire 21 bit word LSB to MSB. Not the most awesome implementation
	uint32_t test2=0;
	if(addressword&0x80000000)test2|=(1UL<<0);
	if(addressword&0x40000000)test2|=(1UL<<1);
	if(addressword&0x20000000)test2|=(1UL<<2);
	if(addressword&0x10000000)test2|=(1UL<<3);
	if(addressword&0x08000000)test2|=(1UL<<4);
	if(addressword&0x04000000)test2|=(1UL<<5);
	if(addressword&0x02000000)test2|=(1UL<<6);
	if(addressword&0x01000000)test2|=(1UL<<7);
	if(addressword&0x00800000)test2|=(1UL<<8);
	if(addressword&0x00400000)test2|=(1UL<<9);
	if(addressword&0x00200000)test2|=(1UL<<10);
	if(addressword&0x00100000)test2|=(1UL<<11);
	if(addressword&0x00080000)test2|=(1UL<<12);
	if(addressword&0x00040000)test2|=(1UL<<13);
	if(addressword&0x00020000)test2|=(1UL<<14);
	if(addressword&0x00010000)test2|=(1UL<<15);
	if(addressword&0x00008000)test2|=(1UL<<16);
	if(addressword&0x00004000)test2|=(1UL<<17);
	if(addressword&0x00002000)test2|=(1UL<<18);
	if(addressword&0x00001000)test2|=(1UL<<19);
	if(addressword&0x00000800)test2|=(1UL<<20);
	return test2;
}

uint8_t getAddressType(uint32_t addressword){
	addressword = decodeAddress(addressword);
	if(addressword==0x1FFFFF){
		return ADDR_IDLE2;
	} else if(addressword>=0x1F7FFF){
		return ADDR_LONG2;
	} else if(addressword>=0x1F7810){
		return ADDR_RESERVED;
	} else if(addressword>=0x1F7800){
		return ADDR_TEMPORARY;
	} else if(addressword>=0x1F6800){
		return ADDR_NETWORKID;
	} else if(addressword>=0x1F2800){
		return ADDR_INFO_SVC;
	} else if(addressword>=0x8001){
		return ADDR_SHORT;
	} else if(addressword>=1){
		return ADDR_LONG1;
	} else {
		return ADDR_IDLE1;
	}
}

void addAlphaMessageContent(struct frame* frame, uint8_t start, uint8_t length, struct message* message){
	// this function adds data to a message. It allocates needed space, or reallocates in case of a continued message
	uint8_t wordcount;
	uint8_t bytecount;
	uint16_t messagebyte;
	uint32_t temp32;
	uint8_t temp8;
	char* tempp = 0;

	
	// decode the header first
	struct alphamessageheader header = decodeAlphaHeader(*getWord(frame,start),*getWord(frame,start+1));
	
	// check if this is the first fragment (or maybe not the first, but we've missed the other fragments...
	if((header.fragmentnumber==3)||(message->messagelength==0)){
		message->messageno = header.messagenumber;
		message->messagelength=((length-1)*3)-header.continued;
		messagebyte=0;
		message->signature=header.signature;
		ATOMIC_BLOCK(ATOMIC_FORCEON){
			// protected calloc
			message->messagep=calloc(message->messagelength,1);
		}
		
	// not the first, there was an earlier fragment. Grow the allocated room for the message. Add one byte if this is the last fragment
	} else {
		messagebyte=message->messagelength;
		message->messagelength+=((length-1)*3)+1-header.continued;
		tempp = message->messagep;
		ATOMIC_BLOCK(ATOMIC_FORCEON){
			// protected realloc
			message->messagep=realloc(message->messagep,message->messagelength);
		}
	}
	
	if(message->messagep==NULL){
		message->messagep = tempp;
		return;
	}
	
	// check if this is the first fragment, as there are 7 extra fragments
	if(header.fragmentnumber==3){
		bytecount=1; // offset by one byte for signature
	} else {
		bytecount=0;
	}
	
	// read message words, each consisting of 3 bytes (except the first one)
	for(wordcount=start+1;wordcount<(start+length);wordcount++){
		temp32=*getWord(frame,wordcount);
		temp8=getValidity(frame,wordcount);
		if(!temp8){
			tempp = message->messagep;
			ATOMIC_BLOCK(ATOMIC_FORCEON){
				message->messagelength +=8;
				// protected
				message->messagep= realloc(message->messagep, message->messagelength); 
			}
			if(message->messagep==NULL){
				message->messagep = tempp;
				return;
			}
			message->messagep[messagebyte]=0x1B;
			messagebyte++;
			message->messagep[messagebyte]=0x5B;
			messagebyte++;
			message->messagep[messagebyte]=0x37;
			messagebyte++;
			message->messagep[messagebyte]=0x6D;
			messagebyte++;		
		}
		for(;bytecount<3;bytecount++){
			if(((char)bitswitch((uint8_t)(temp32>>(24-(7*bytecount))))&0x7F)>0x1F){
				(message->messagep)[messagebyte]= (char)bitswitch((uint8_t)(temp32>>(24-(7*bytecount))))&0x7F;
				messagebyte++;
			} else if(!temp8){
				(message->messagep)[messagebyte]= 0xDB;
				messagebyte++;
			}
		}
		bytecount=0;
		if(!temp8){
			message->messagep[messagebyte]=0x1B;
			messagebyte++;
			message->messagep[messagebyte]=0x5B;
			messagebyte++;
			message->messagep[messagebyte]=0x30;
			messagebyte++;
			message->messagep[messagebyte]=0x6D;
			messagebyte++;
		}
	}
	
	// if this is the final message, add a null character
	if(header.continued==0){
		(message->messagep)[messagebyte]=0; // null character termination
		message->iscomplete=1;
		message->timeout=0;
	}
	
}

struct alphamessageheader decodeAlphaHeader(uint32_t firstword,uint32_t secondword){
	// decodes the entire alphamessage header
	struct alphamessageheader header;
	header.word = firstword;
	header.fragmentcheck=bitswitch((uint8_t)(firstword>>24));
	header.fragmentcheck|=(uint16_t)(bitswitch((uint8_t)(firstword>>16))&0x03)<<8;
	firstword>>=4;
	header.maildrop=bitswitch((uint8_t)firstword)&0x01;
	firstword>>=1;
	header.retrieval=bitswitch((uint8_t)firstword)&0x01;
	firstword>>=6;
	header.messagenumber=bitswitch((uint8_t)firstword)&0x3F;
	firstword>>=2;
	header.fragmentnumber=bitswitch((uint8_t)firstword)&0x03;
	firstword>>=1;
	header.continued=bitswitch((uint8_t)firstword)&0x01;
	
	// if this is the first fragment, decode the signature as well
	if(header.fragmentnumber==3){
		secondword>>=24;
		header.signature=bitswitch((uint8_t)secondword)&0x7F;
		//header.signature=(uint8_t)(bitswitch((uint8_t)(secondword>>24))&0x7F);
	}
	return header;
}

struct message* findMessage(uint32_t address, uint8_t messageno){
	// this function attempts to find a parked message that was fragmented. If the message is found, it's pointer is returned
	uint8_t count;
	for(count=0;count<MAX_MESSAGES;count++){
		if(messages[count]->primaryaddresss==address){
			if(messages[count]->messageno==messageno){
				// message found, return pointer
				return messages[count];
			}
		}
	}
	// no message found
	return 0;
}

void deleteStaleMessages(){
	char* tempp;
	// in order to delete parked messages that aren't ever finished due to errors, messages time-out. This function
	// decreases a timeout counter, and deletes the message if it reaches zero
	uint8_t count;
	for(count=0;count<MAX_MESSAGES;count++){
		if(messages[count]){
			if(messages[count]->timeout==0){
				tempp = messages[count]->messagep;
				ATOMIC_BLOCK(ATOMIC_FORCEON){
					// protected
					messages[count]->messagep = realloc(messages[count]->messagep,messages[count]->messagelength+1);
				}
				if(messages[count]->messagep==NULL){
					messages[count]->messagep = tempp;
					messages[count]->messagep[(messages[count]->messagelength)-1]=0x00;
				} else {
					messages[count]->messagep[messages[count]->messagelength]=0x00;
				}
				#ifndef SERDEBUG
					outputMessageParse(messages[count]);
				#endif
				#ifdef SERDEBUG
					outputMessage(messages[count]);
				#endif
				uart_puts_P("[MSG TRUNCATED]\n\r");
				cleanUpMessage(messages[count]);
				messages[count]=0;
				#ifdef SERDEBUG
					uart_puts_P("| ==-- Message expired, deleted --==\r\n");
				#endif
			} else {
				messages[count]->timeout--;
			}
		}
	}
}

void outputMessage(struct message* msg){
	// outputs the message in a debug-format
	uint8_t count;
	for(count=0;count<(msg->addresslist.addresscount);count++){
		uart_puts_P("|\tADDR:");ultoa(msg->addresslist.addresspointer[count]-32768, buffer, 10);uart_puts(buffer);uart_puts_P("\r\n");
	}
	uart_puts_P("|   ");uart_puts(msg->messagep);
	uart_puts_P("\r\n");
}

void outputMessageParse(struct message* msg){
	// outputs the message in a parseable format
	uint8_t count;
	uart_puts_P("[[msg]]\n\r");
	for(count=0;count<(msg->addresslist.addresscount);count++){
		uart_puts_P("[[addr]]");ultoa(msg->addresslist.addresspointer[count]-32768, buffer, 10);uart_puts(buffer);uart_puts_P("\n\r");
	}
	uart_puts_P("[[data]]");
	uart_puts(msg->messagep);
	uart_puts_P("[[/data]]\n\r[[/msg]]\n\r");
}

void storeMessage(struct message* msg){
	// save fragmented message, to be finished later
	uint8_t count;
	for(count=0;count<MAX_MESSAGES;count++){
		if(messages[count]){
			// slot occupied
		} else {
			// slot free, save message in slot
			messages[count]=msg;
			msg->location=count;
			return;
		}
	}
	// if no slot available, discard the message.
	cleanUpMessage(msg);
	#ifdef SERDEBUG
	uart_puts_P("-- Message deleted, no slots available :( \r\n");
	#endif
}

void processFrame(struct frame* frame){
	// this function is the entrypoint for frame processing.
	uint8_t avcount;
	uint8_t counter;
	uint8_t counter2;
	struct vector vect; //
	struct alphamessageheader head;
	struct message* msg;
	#ifdef SERDEBUG
		uint8_t timer = sys.subsecond;
	#endif
	// check for mutex, set if not set
	cli();
	if(procmutex==0){
		procmutex=1;
	} else {
		sei();
		// output a great big warning if processFrame was called while another was active
		#ifdef SERDEBUG
			uart_puts_P("!!!! - processFrame called while other frame was being processed! This isn't supposed to happen!!!!\n\r\n\n\n");
		#endif
		cleanUpFrame(frame);
		return;
	}
	sei();
	
	// first, validate the BIW at word 0. Try to repair errors up to 2 bits
	switch(validateWord(frame,0,VALIDATE_FLEX_CHECKSUM|REPAIR2)){
		case REPAIRED_1:
			#ifdef SERDEBUG
				uart_puts_P("-- Recovered BIW with 1 bit error");
			#endif
			processBIW(frame);
			break;
		case REPAIRED_2:
			#ifdef SERDEBUG
				uart_puts_P("-- Recovered BIW with 2 bit error");
			#endif
			processBIW(frame);
			break;
		case VALIDATE_PASS:
			processBIW(frame);
			break;
		default:
		case VALIDATE_FAIL:
			// BIW failed the checksum and was unrepairable. We're gonna have to get rid of the entire frame
			#ifdef SERDEBUG
				uart_puts_P("-- Unable to validate/repair BIW for frame ");itoa(frame->fiw.frame, buffer, 10);uart_puts(buffer);
				uart_puts_P(", frame discarded\n\r");
			#endif
			cleanUpFrame(frame);
			procmutex=0;
			return;
			break;		
	}
		
	// delete stale mappings for frames that weren't transmitted in this cycle. If there were frames in between
	// the previously parsed frame and this one, delete mappings for those frames as well
	if(previousframe==0xFF){
		previousframe=frame->fiw.frame;
	} 
	for(counter=((previousframe+1)%128);counter!=frame->fiw.frame;counter=((counter+1)%128)){
		clearMappings(counter);
	}
	previousframe=frame->fiw.frame;
	 
	
	// start serial output information
	#ifdef SERDEBUG
		uart_puts_P("+FRAME ");
		uart_puts_P("C:");itoa(frame->fiw.cycle, buffer, 10);uart_puts(buffer);
		uart_puts_P(" F:");itoa(frame->fiw.frame, buffer, 10);uart_puts(buffer);
		uart_puts_P(" LENGTH:");itoa((frame->biw.carryon)+1, buffer, 10);uart_puts(buffer);
		uart_puts_P(" BI-LEN:");itoa(frame->biw.endofblockinfo, buffer, 10);uart_puts(buffer);
		uart_puts_P(" VECT: ");itoa(frame->biw.vectorstart, buffer, 10);uart_puts(buffer);
		uart_puts_P(" PRIORITY ADR: ");itoa(frame->biw.priority, buffer, 10);uart_puts(buffer);
		uart_puts_P(" Signal: ");itoa(rssi.avgblock, buffer, 10);uart_puts(buffer);
		uart_puts_P(" Noise: ");itoa(rssi.avgnoise, buffer, 10);uart_puts(buffer);
		uart_puts_P(" used: ");ultoa((uint16_t)getMemoryUsed(), buffer, 10);uart_puts(buffer);uart_puts_P(" bytes");
		uart_puts_P("\r\n");
	#endif
	#ifndef SERDEBUG
		uart_puts_P("[[frame]]");itoa(frame->fiw.cycle, buffer, 10);uart_puts(buffer);uart_puts_P("|");
		itoa(frame->fiw.frame, buffer, 10);uart_puts(buffer);uart_puts_P("\n\r");
	#endif
	
	
	// check if there's multiple BIWs. Yeah I know, they're processed out of order. So what. These words will also be 2-bit recovered
	switch(frame->biw.endofblockinfo){
		case 0x03:
			if(validateWord(frame,3,REPAIR2|VALIDATE_FLEX_CHECKSUM))processBIW2(frame->block[0]->word[3]);
		case 0x02:
			if(validateWord(frame,2,REPAIR2|VALIDATE_FLEX_CHECKSUM))processBIW2(frame->block[0]->word[2]);
		case 0x01:
			if(validateWord(frame,1,REPAIR2|VALIDATE_FLEX_CHECKSUM))processBIW2(frame->block[0]->word[1]);		
	}
	
	// if this is a so-called idle block, output this information
	#ifdef SERDEBUG
	if((frame->biw.vectorstart==1)&&(frame->biw.endofblockinfo==0)){
		uart_puts_P("| IDLE...\n\r");
	}
	#endif
	
	// determine length of address and vector field
	avcount = (frame->biw.vectorstart-frame->biw.endofblockinfo)-1;

	
	// validate all vector checksums, delete if invalid
	for(counter=0;counter<avcount;counter++){
		switch(validateWord(frame,counter+frame->biw.vectorstart,REPAIR2|VALIDATE_FLEX_CHECKSUM)){
			case REPAIRED_2:
				#ifdef SERDEBUG
					uart_puts_P("2-bit error in vector repaired! \r\n");
				#endif
			case VALIDATE_PASS:
			case REPAIRED_1:
				break;
			case VALIDATE_FAIL:
				#ifdef SERDEBUG
					uart_puts_P("Irrepairable vector discarded :(\r\n");
				#endif
				*getWord(frame,counter+frame->biw.vectorstart)=0;
				break;
		}
	}

	// find all vectors of the non-instruction type
	for(counter=0;counter<avcount;counter++){
		// decode the first vector
		validateWord(frame,counter+(frame->biw.addressstart),REPAIR2);
		vect = decodeVector(*getWord(frame,counter+frame->biw.vectorstart),*getWord(frame,counter+(frame->biw.addressstart)));
		switch(vect.type){
			case VECT_ALPHA:
				// delete the vector
				*getWord(frame, counter+frame->biw.vectorstart) = 0;
				
				// decode the message header
				head = decodeAlphaHeader(*getWord(frame,vect.start),*getWord(frame,1+vect.start));

				// check if it is an initial fragment (always 0x03);
				if(head.fragmentnumber!=0x03){
					// see if we can find a message with this number and address			
					msg=findMessage(vect.address,head.messagenumber);
				} else {
					// new message;
					msg = 0;
				}
				
				if(msg){
					// initial message retrieved, clear the slot (not doing this would break multipart messages with more than 2 parts)
					messages[msg->location]=0x00;
					msg->location=NO_LOC_ASSIGNED;
				} else {
					msg = addMessage();
					
					// check if we were able to allocate the space for a message
					if(msg==NULL){
						cleanUpFrame(frame);
					}
					addAddressToMessage(vect.address,frame->fiw.frame,msg);
					msg->primaryaddresss = vect.address;
					for(counter2=counter+1;counter2<avcount;counter2++){
						if(decodeVector(*getWord(frame, counter+frame->biw.vectorstart),*getWord(frame, counter+frame->biw.addressstart)).start==vect.start){
							// found another vector to the same message. output address and delete vector
							*getWord(frame,counter2+frame->biw.vectorstart)=0;// found and decoded, clear the vector in the block/word
							addAddressToMessage(decodeVector(*getWord(frame, counter+frame->biw.vectorstart),*getWord(frame, counter+frame->biw.addressstart)).address,frame->fiw.frame,msg);
						}
					}
				}
				
				// Save message to struct;
				validateWord(frame,vect.start,REPAIR2);
				addAlphaMessageContent(frame,vect.start,vect.length,msg);
				
				// check if this is a complete message, or if it's continued later
				if(msg->iscomplete){
					#ifndef SERDEBUG
					outputMessageParse(msg);
					#endif
					#ifdef SERDEBUG
					outputMessage(msg);
					#endif
					cleanUpMessage(msg);
				} else {
					// incomplete message, store for further completion
					storeMessage(msg);
				}	
				break;
		}
	}
	
	// remove all the mappings for this frame
	clearMappings(frame->fiw.frame);
	
	// make new mappings (process all instruction vectors)
	for(counter=0;counter<avcount;counter++){
		vect = decodeVector(*getWord(frame, counter+frame->biw.vectorstart),*getWord(frame, counter+frame->biw.addressstart));
		if(vect.type==VECT_INSTRUCTION)addMapping(vect.tempframe,vect.tempaddr,vect.address);
	}
	
	// check if some unfinished messages have perished
	deleteStaleMessages();
	
	//cleanup this frame
	cleanUpFrame(frame);
	
	#ifndef SERDEBUG
		uart_puts_P("[[/frame]]\n\r");
		uart_putc(0x08);
	#endif
	#ifdef SERDEBUG
		uint16_t timer2 = sys.subsecond;
		uint16_t subtimer = TCNT0;
		subtimer*=8;
		subtimer/=125;
		if(timer2<timer){
			ultoa((uint16_t)((1000*((timer2+125))-timer)/125)+subtimer, buffer, 10);
		} else {
			ultoa((uint16_t)((1000*((timer2)-timer))/125)+subtimer, buffer, 10);
		}
		uart_puts_P("\\_______________________________________ Frame processed in ");
		uart_puts(buffer);
		uart_puts_P(" ms - Memory used/free: ");ultoa((uint16_t)getMemoryUsed(), buffer, 10);uart_puts(buffer);uart_puts_P("/");ultoa((uint16_t)getFreeMemory(), buffer, 10);uart_puts(buffer);uart_puts_P(" bytes\r\n");
	#endif
	
	// unset mutex
	procmutex = 0;
}