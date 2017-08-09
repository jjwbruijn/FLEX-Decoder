/**
 *  @file
 *  @defgroup Jelmers FLEX decoder second stage <flex.h>
 *  @code #include <flexprocess.h> @endcode
 * 
 *  @brief The second stage processes the frames as received by the first stage.
 *	Entrypoint is processFrame(*frame)
 *
 *  @note Based on US patent US55555183
 *  @author Jelmer Bruijn
 */


#ifndef FLEXPROCESS_H_
#define FLEXPROCESS_H_

// Configurable
// This defines how many mapping structs may be valid at any given time
#define MAX_MAPPINGS 8

// How long (long) messages last until they timeout (in frames)
#define LONG_MSG_TTL 10

// how many simultaneous messages can be stored
#define MAX_MESSAGES 5


// mapping struct. A mapping like this is created for a short instruction vector
struct mapping {
	uint8_t tempaddress;
	uint32_t *addressp;
	uint8_t addresscount;
	uint8_t frame;	
};

// alpha/hex/binary/secure/short-instruction vector
struct vector{
	uint8_t length;
	uint8_t start;
	uint32_t address;
	uint8_t addresstype;
	uint8_t tempaddr;
	uint8_t tempframe;
	uint8_t type;	
};

// struct to hold the decoded alpha-message header information
struct alphamessageheader {
	uint16_t fragmentcheck;
	uint8_t continued;
	uint8_t fragmentnumber;
	uint8_t messagenumber;
	uint8_t retrieval;
	uint8_t maildrop;
	uint8_t signature;
	uint32_t word;
};

// message struct, holds an entire message
struct message {
	uint32_t primaryaddresss;
	char* messagep;
	uint16_t messagelength;
	uint8_t messageno;
	uint8_t signature;
	uint8_t sigtemp;
	uint8_t timeout;
	uint8_t iscomplete;
	uint8_t location;
	struct addresslist {
		uint32_t* addresspointer;
		uint8_t addresscount;
	} addresslist;
};

// vector types
#define VECT_SECURE 0
#define VECT_INSTRUCTION 1
#define VECT_SHORT 2
#define VECT_NUMERIC 3
#define VECT_NUMERIC_FORMAT 4
#define VECT_ALPHA 5
#define VECT_HEX 6
#define VECT_NUMERIC_NO 7
#define VECT_NULL 0xFF

// address types
#define ADDR_IDLE1 0
#define ADDR_LONG1 1
#define ADDR_SHORT 2
#define ADDR_INFO_SVC 3
#define ADDR_NETWORKID 4
#define ADDR_TEMPORARY 5
#define ADDR_RESERVED 6
#define ADDR_LONG2 7
#define ADDR_IDLE2 8

// no message location assigned flag
#define NO_LOC_ASSIGNED 0xFF

volatile uint8_t procmutex;

/** @brief  Decodes given vector and produces a struct containing relevant info
 *  @param  vword vector-word
 *	@param	address Addressword (relevant for short instruction vectors)
 *	@return The decoded vector
 */
struct vector decodeVector(uint32_t vword, uint32_t address);

/** @brief  Returns the validity of any word in the frame
 *  @param  frame Pointer to the frame that will be checked
 *	@param	word that shall be checked
 *	@return 1 if the word is valid, 0 if the word contains unrecovered errors
 */
uint8_t getValidity(struct frame* frame, uint8_t word);

/** @brief  Processes the Block Information Word for the frame, and stores it in the frame structure itself
 *  @param  frame Pointer to the frame containing the BIW
 */
void processBIW(struct frame* frame);

/** @brief  Processes the extended BIW for given frame, stored in the 'sys' structure
 *  @param  frame Pointer to the frame containing the BIW
 */
void processBIW2(uint32_t biwword);

/** @brief	Initializes some stuff for the processor, setting up and clearing the buffers
 */
void initProcessor(void);

/** @brief	Recursively removes a message and all associated allocated space
 *  @param  msg Pointer to the message that shall be cleaned up
 */
void cleanUpMessage(struct message* msg);

/** @brief  Create a new allocated message and sets up relevant values to their default state
 *  @return Pointer to the created (empty) message
 */
struct message* addMessage();

/** @brief  Removes all mappings for any given frame
 *  @param  curframe Current frame-number
 */
void clearMappings(uint8_t curframe);

/** @brief  Adds a mapping to the mapping-table
 *  @param  frame framenumber for the mapping
 *	@param	tempaddress temporary address for mapping
 *	@param	address	address for which the mapping is valid
 */
uint8_t addMapping(uint8_t frame, uint8_t tempaddress, uint32_t address);


/** @brief	Adds all valid mappings for a specific temporary address and frame to the addresslist in the message
 *	@param	address Address to find mappings for
 *  @param  frame framenumber for the mapping
 *	@param	msg Message to add the addresses to
 */
void addMappingsToMessage(uint32_t address,uint8_t frame, struct message* msg);

/** @brief	Adds an address to the message, growing the messagelist by one
 *	@param	address Address to find mappings for
 *  @param  frame framenumber for mappings as needed
 *	@param	msg Message to add the addresses to
 */
void addAddressToMessage(uint32_t address, uint8_t frame, struct message* msg);

/** @brief	Decodes an address from a message (currently, only short addresses are supported)
 *	@param	addressword The word containing the address that needs to be decoded
 *	@return Decoded address
 */
uint32_t decodeAddress(uint32_t addressword);

/** @brief	Decodes the address type from a message (currently unused)
 *	@param	addressword The word containing the address
 *	@return Decoded address type (see #defines)
 */
uint8_t getAddressType(uint32_t addressword);


/** @brief  Adds alphanumeric content to a message struct
 *  @param  frame Pointer to the frame that contains the content
 *	@param	start Word that contains the alphanumeric header
 *	@param	length Total amount of words in the alpha message
 *	@param	message pointer to the message where the content will be added
 */
void addAlphaMessageContent(struct frame* frame, uint8_t start, uint8_t length, struct message* message);

/** @brief	Decodes the header for an alphanumeric message
 *  @param  firstword The first header word (the majority)
 *	@param	secondword Second header word (only applicable for the first fragment)
 */
struct alphamessageheader decodeAlphaHeader(uint32_t firstword,uint32_t secondword);

/** @brief	Finds a stored message, by message number and address
 *	@param	address The first (usually only) address for which the message is valid
 *	@param	messageno Message identification number
 *	@return Pointer to the stored message
 */
struct message* findMessage(uint32_t address, uint8_t messageno);

/** @brief	Checks if there are any lingering messages in the buffer that werent finished. Decrements TTL counter
 *			on all messages
 */
void deleteStaleMessages();

/** @brief	Prints message and it's addressee's in a debug format
 *	@param	msg Pointer to the message
 */
void outputMessage(struct message* msg);

/** @brief	Prints message and it's addressee's in a parseable format
 *	@param	msg Pointer to the message
 */
void outputMessageParse(struct message* msg);

/** @brief	Stores the message in the buffer in order to add more fragments later
 *	@param	msg Pointer to the message
 */
void storeMessage(struct message* msg);

/** @brief	Processes the frame (entrypoint for frame processor)
 *	@param	frame Pointer to the frame
 */
void processFrame(struct frame* frame);
#endif /* FLEXPROCESS_H_ */