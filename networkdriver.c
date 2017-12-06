/*
Yufang Lin
Duck ID: yufang
CIS 415 Project 2
This is my own work except I:
	Referenced: Lab6 & 7 for pthread code
*/

#include <pthread.h>							//pthread_t, pthread_create()
#include <stdio.h>
#include <stdbool.h>							// bool

#include "diagnostics.h"						//	DIAGNOSTICS
#include "packetdescriptor.h"					// init_packet_descriptor(), packet_descriptor_get_pid()
#include "pid.h"								// PID typedef and MAX_PID
#include "freepacketdescriptorstore.h"			// blocking_get_pd(), nonblocking_get_pd()
													// blocking_put_pd(), nonblocking_put_pd()

#include "networkdevice.h"						// send_packet(), register_receiving_packetdescriptor(),
													// await_incoming_packet() 
#include "freepacketdescriptorstore__full.h"		// create_fpds(), destroy_fpds()?
#include "packetdescriptorcreator.h"			// create_free_packet_descriptor()  
#include "BoundedBuffer.h"					  	// createBB(), blockingWriteBB(), nonblockingWriteBB(),
											 		// blockingReadBB(), nonblockingReadBB()

////////	GLOBAL VARIABLES	//////
static NetworkDevice *networkDevice = NULL;
static FreePacketDescriptorStore *fpds = NULL;

#define MAXBUFFER 100 							// max buffer size 
#define MAXTRIES 5								// send_packet max tries


BoundedBuffer *sendBuffer = NULL;				// buffer for send
BoundedBuffer *getBuffer[MAXBUFFER] = {NULL};	// buffer for receiving packets

static bool initCalledFlag  = false;			// check to prevent multiple calls of init
static bool sendBufferFlag = false;				// check to make sure send thread been initialized
static bool getBufferFlag = false;				// check to make sure get thread been initialized

////////	INNER FUNCTIONS		//////
static void *sending_thread_func() {
	/*
	*  thread that deals with sending packets to network device
	*  loops and attempts to packets until packet sent or MAXTRIES reached
	*/

	// Double check init already been called
	if(initCalledFlag == false){
		return NULL;
	}

	// set thread flag to true, so networkdriver knows thread has been created
	sendBufferFlag = true;

	// local packetdescriptor variable
	void *packet = NULL;
	// feedback from send_packet variable
	int sendFeedback = 0;
	// max tries the send_packet will be called before giving up
	int maxTries;

	while(true){
		maxTries = MAXTRIES;
		//take packet from send buffer
		packet = blockingReadBB(sendBuffer);
		//attempt to send packet to networkDevice
		//sendFeedback = send_packet(networkDevice, packet);
		//check to see if packet is sent, if not try a couple of times
		while(sendFeedback == 0 && maxTries > 0){
			DIAGNOSTICS("re-attempting to send packet to network device\n");
			//decrement maxTries
			--maxTries;
			sendFeedback = send_packet(networkDevice, (PacketDescriptor *) packet);

			//check to see if send_packet was successful at all
			if(sendFeedback == 0 && maxTries == 0){
				DIAGNOSTICS("sending packet to network device has failed\n");
			}
		}

		//return packet to store

		if(nonblocking_put_pd(fpds, packet) == 0){
			printf("----------------STORE FAILED-------\n" );
		}
	}

}

static void *get_thread_func(){
	/*
	*	recieves packet from network device
	*/

	// Double check init already been called
	if(initCalledFlag == false){
		return NULL;
	}

	getBufferFlag = true;

	// local packet variable
	PacketDescriptor *packet = NULL;
	// local packet pid variable
	PID packetPid;
	// feedback on nonblocking write for buffer 
	int feedbackBB = 0;

	while(true){
		//get packet from store		
		DIAGNOSTICS("----------------ATTEMPTING TO GET PACKET------\n");

		blocking_get_pd(fpds, &packet);
		DIAGNOSTICS("----------------GOT PACKET------\n");

		//initialize packet
		init_packet_descriptor(packet);
		//register packet
		register_receiving_packetdescriptor(networkDevice, packet);
		//await incoming packet
		await_incoming_packet(networkDevice);

		//get pid of the packet
		packetPid = packet_descriptor_get_pid(packet);

		//write packet to get buffer
		feedbackBB = nonblockingWriteBB(getBuffer[packetPid], (void **)packet);

		//check if packet was written to buffer.
		if(feedbackBB == 0){
			DIAGNOSTICS("write to buffer failed\n");
			blocking_put_pd(fpds, packet);
		}

	}
}


static void cleanup(){
	/*
	* When init fails, need to clean up before returning
	* this function deals with the cleaning part
	*/

	//  clean up the store   //
	//check that store was created
	if(fpds){
		//destroy it
		destroy_fpds(fpds);
	}

	//	clean up send buffer
	//check send buffer was already created
	if(sendBuffer){
		destroyBB(sendBuffer);
	}

	//	clean up get buffer
	//because the get buffer is an array of buffers, loop through
	int i;
	for(i = 0; i < MAX_PID+1; i++){
		//check if buffer is NULL
		if(getBuffer[i] == NULL){ 
			destroyBB(getBuffer[i]);
		}
	}


	initCalledFlag = false;
}

///////		UPPER EDGE FUNCTIONS	//////

/* These calls hand in a PacketDescriptor for dispatching */
/* Neither call should delay until the packet is actually sent      */

void blocking_send_packet(PacketDescriptor *pd){
	/* The blocking call will usually return promptly, but there may be */
	/* a delay while it waits for space in your buffers.                */

	//check to see if init was called (and successfully executed)
	if(initCalledFlag == false){
		return;
	}

	DIAGNOSTICS("-------- BLOCKING_SEND --------\n");
	//write packet into send buffer
	blockingWriteBB(sendBuffer, (void*) pd);

	DIAGNOSTICS("--WRITE SUCCESSFUL--\n");
}

int  nonblocking_send_packet(PacketDescriptor *pd){
	/* The nonblocking call must return promptly, indicating whether or */
	/* not the indicated packet has been accepted by your code          */
	/* (it might not be if your internal buffer is full) 1=OK, 0=not OK */

	//check to see if init was called (and successfully executed)
	if(initCalledFlag == false){
		return 0;
	}

	DIAGNOSTICS("-------- NONBLOCKING_SEND --------\n");
	//nonblockWrite buffer feedback variable
	int feedbackBB = 0; 

	//store packet 
	feedbackBB = nonblockingWriteBB(sendBuffer, pd);

	if(feedbackBB == 0){
		DIAGNOSTICS("--WRITE to buffer NOT successfull--\n");
	}
	else{
		DIAGNOSTICS("--WRITE to buffer SUCCESSFUL-- \n");
	}

	//return whether storing was successful or not
	return feedbackBB; 

}


/* These represent requests for packets by the application threads */
/* Both calls indicate their process number and should only be     */
/* given appropriate packets. You may use a small bounded buffer   */
/* to hold packets that haven't yet been collected by a process,   */
/* but are also allowed to discard extra packets if at least one   */
/* is waiting uncollected for the same PID. i.e. applications must */
/* collect their packets reasonably promptly, or risk packet loss. */

void blocking_get_packet(PacketDescriptor **pd, PID pid){
	/* The blocking call only returns when a packet has been received  */
	/* for the indicated process, and the first arg points at it.      */

	//check to see if init was called (and successfully executed)
	if(initCalledFlag == false){
		return;
	}

	DIAGNOSTICS("-------- BLOCKING_get --------\n");
	//get packet from get buffer with the given pid
	//and set it to the given packet descriptor
	*pd = blockingReadBB(getBuffer[pid]);

	DIAGNOSTICS("--READ SUCCESSFUL--\n");

}



int  nonblocking_get_packet(PacketDescriptor **pd, PID pid){
	/* The nonblocking call must return promptly, with the result 1 if */
	/* a packet was found (and the first argument set accordingly) or  */
	/* 0 if no packet was waiting.                                     */

	//check to see if init was called (and successfully executed)
	if(initCalledFlag == false){
		return 0;
	}

	DIAGNOSTICS("-------- NONBLOCKING_GET --------\n");
	//nonblocking read buffer feedback variable
	int feedbackBB = 0;

	//get from buffer
	feedbackBB = nonblockingReadBB(getBuffer[pid], (void**) pd);

	if(feedbackBB == 0){
		DIAGNOSTICS("--READ to buffer NOT successfull--\n");
	}
	else{
		DIAGNOSTICS("--READ to buffer SUCCESSFUL-- \n");
	}

	//return feedback on whether or not read was succesful
	return feedbackBB;
}



/* Called before any other methods, to allow you to initialise */
/* data structures and start any internal threads.             */ 
/* Arguments:                                                  */
/*   nd: the NetworkDevice that you must drive,                */
/*   mem_start, mem_length: some memory for PacketDescriptors  */
/*   fpds_ptr: You hand back a FreePacketDescriptorStore into  */
/*             which you have put the divided up memory        */
/* Hint: just divide the memory up into pieces of the right size */
/*       passing in pointers to each of them                     */ 
void init_network_driver(NetworkDevice *nd, 
                         void *mem_start, 
                         unsigned long mem_length,
                         FreePacketDescriptorStore **fpds_ptr){

	//first check if the given args are appropriate
	if(nd == NULL || mem_start == NULL || mem_length == 0 || fpds_ptr == NULL || *fpds_ptr != NULL || initCalledFlag == true){
		return;
	}

	//set init flag to true to prevent multiple init calls
	initCalledFlag = true;

	//since check passed start setting global values
	//first, networkDevice
	networkDevice = nd;

	//2nd, create fpd store and set global values
	fpds = create_fpds();
	*fpds_ptr = fpds;		//have global point to store to use within networkdriver.c

		// populate store with packets 
	create_free_packet_descriptors(fpds, mem_start, mem_length);

	//3rd, set the buffers
	sendBuffer = createBB(MAXBUFFER);
		// check if buffer is properly allocated.
	if(sendBuffer == NULL){
		DIAGNOSTICS("send buffer creation failed\n");
		//clean up allocated memory and flags
		cleanup();
		return;
	}

	int i;
	for(i = 0; i < MAX_PID+1; i++){
		getBuffer[i] = createBB(MAXBUFFER);
		//check if buffer is properly allocated
		if(getBuffer[i] == NULL){ 
			DIAGNOSTICS("get buffer creation failed\n");
			//clean up allocated memory and flags
			cleanup();
			return;
		}
	}

	//4th, create threads
		// thread variables
	pthread_t sendThread;
	pthread_t getThread;


	if(pthread_create(&sendThread, NULL, sending_thread_func, NULL)){
		//clean up allocated memory and flags
		cleanup();
		DIAGNOSTICS("Error creating send thread\n");
		return;
	}

	pthread_detach(sendThread);

	if(pthread_create(&getThread, NULL, get_thread_func, NULL)){
		//clean up allocated memory and flags
		cleanup();
		DIAGNOSTICS("Error creating get thread\n");
		return;
	}

	pthread_detach(getThread);
	
}
