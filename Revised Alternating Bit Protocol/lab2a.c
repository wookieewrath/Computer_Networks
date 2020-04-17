#include <cnet.h>
#include <stdlib.h>
#include <string.h>

/************************************************************
*   Submitted by: Neel Kumar                                *
*                                                           *
*   The structure for this code is borrowed from            *  
*   stopandwait.c provided on eClass + CNET documentation   *
*                                                           *
*************************************************************/

/*******************************************************************************
*                              GLOBAL DECLARATIONS                             *
*******************************************************************************/
// Struct containing the message
typedef struct {
    char        data[MAX_MESSAGE_SIZE];
} MSG;

// Struct for a FRAME that will be sent over the physical layer
typedef struct {
    size_t	     len;       
    int          checksum;  	
    int          seq;       	
    int          ack;
    CnetAddr     destaddr;
    CnetAddr     srcaddr;
    MSG          msg;
} FRAME;

// Struct to hold the parameters for each connection
// In our two host scenario, each host will only have one CONN
typedef struct{
    MSG       	*lastmsg;
    size_t		lastlength;
    CnetTimerID	lasttimer;
    CnetAddr    other_address;
    int       	ackexpected;
    int		    nextframetosend;
    int		    frameexpected;
} CONN;

// Globally define a CONN
static CONN conn;

// Definitions for the FRAME size calculations
#define FRAME_HEADER_SIZE  (sizeof(FRAME) - sizeof(MSG))
#define FRAME_SIZE(f)      (FRAME_HEADER_SIZE + f.len)


/*******************************************************************************
*                                TRANSMIT FRAME                                *
*******************************************************************************/
static void transmit_frame(MSG *msg, size_t length, int seqno, int ackno, CnetAddr destination)
{
    // Initialize a frame and a link
    FRAME       f;
    int		link = 1;

    // Set the frame attributes based on the inputs given in transmit_frame
    f.seq       = seqno;
    f.ack       = ackno;
    f.checksum  = 0;
    f.len       = length;
    f.destaddr  = destination;
    f.srcaddr   = nodeinfo.address;
    
    // If an ack frame is being sent, not much to do
    // If a data frame is being sent (not forwarded!), start a timer
    if (f.ack > -1) {
        printf("ACK sent: (src=%d, dest=%d, f.seq=%d f.ack=%d, msgLen=%d)\n", f.srcaddr, f.destaddr, f.seq, f.ack, f.len);
    }
    else if(f.seq > -1){    
        CnetTime timeout;
        
        printf("Data transmitted: (src=%d, dest=%d, f.seq=%d, f.ack=%d, msgLen=%d)\n", f.srcaddr, f.destaddr, f.seq, f.ack, f.len);
        memcpy(&f.msg, msg, (int)length);

        timeout = FRAME_SIZE(f)*((CnetTime)8000000 / linkinfo[link].bandwidth) +
                    linkinfo[link].propagationdelay;

        conn.lasttimer = CNET_start_timer(EV_TIMER1, 3 * timeout, 0);
    }
    
    // Determine the length and checksum, and send to physical layer
    length      = FRAME_SIZE(f);
    f.checksum  = CNET_ccitt((unsigned char *)&f, (int)length);
    CHECK(CNET_write_physical(link, &f, &length));
}


/*******************************************************************************
*                               APPLICATION_READY                              *
*******************************************************************************/
static EVENT_HANDLER(application_ready)
{
    // Initialize the required parameters for the CNET_read_application call
    CnetAddr destaddr;
    conn.lastlength  = sizeof(MSG);
    
    // Store the read information into the initialized variables and disable sending further messages
    CHECK(CNET_read_application(&destaddr, conn.lastmsg, &conn.lastlength));
    CNET_disable_application(ALLNODES);

    // The 'other address' in this case is simply the only available dest to send messages to
    conn.other_address = destaddr;

    // Call the transmit_frame function to complete processing before sending to physical layer
    printf("down from application, ackexpect=%d, frameexpect=%d, nextframe=%d, dest=%d\n", conn.ackexpected, conn.frameexpected, conn.nextframetosend, destaddr);
    transmit_frame(conn.lastmsg,conn.lastlength, conn.nextframetosend, -1, destaddr);
    conn.nextframetosend++;
}


/*******************************************************************************
*                                PHYSICAL_READY                                *
*******************************************************************************/
static EVENT_HANDLER(physical_ready)
{
    // Initialize variables for CNET_read_physical, and for the checksum
    FRAME        f;
    size_t	     len;
    int          link, checksum;
    len         = sizeof(FRAME);
    
    // Read the data that has arrived on the physical layer
    CHECK(CNET_read_physical(&link, &f, &len));

    // Perform the checksum, and return if a bad frame was sent
    checksum    = f.checksum;
    f.checksum  = 0;
    if(CNET_ccitt((unsigned char *)&f, (int)len) != checksum) {
        printf("\tBAD frame: checksums(stored=%d, computed=%d)\n", checksum, CNET_ccitt((unsigned char *)&f, (int)len));
        return;           // bad checksum, ignore frame
    }

    // When an ack arrives, check if it is the ack expected
    // If so, increment ack expected and re-enable the application layer
    if (f.ack > -1) {
        if(f.ack == conn.ackexpected) {
            printf("\tACK received: (src=%d, dest=%d, f.seq=%d, f.ack=%d, msgLen=%d)\n", f.srcaddr, f.destaddr, f.seq, f.ack, f.len);
            CNET_stop_timer(conn.lasttimer);
            conn.ackexpected++;
            CNET_enable_application(ALLNODES);
        }
        else{
            printf("\t\tACK NOT received, seq=%d, ack=%d, ackexpect=%d\n", f.seq, f.ack, conn.ackexpected);
        }
    }
    // When a data frame arrives, check if it is the correct seq, and if so, write to the application layer
    // Increment frame expected and transmit the ack
    else if(f.seq > -1){
        printf("\tDATA received: (src=%d, dest=%d, f.seq=%d, f.ack=%d, msgLen=%d)", f.srcaddr, f.destaddr, f.seq, f.ack, f.len);
        if(f.seq == conn.frameexpected && nodeinfo.address == f.destaddr) {
            printf(" up to application\n");
            len = f.len;
            CHECK(CNET_write_application(&f.msg, &len));
            conn.frameexpected++;
        }
        else{
            printf("ignored\n");
        }
        transmit_frame(NULL, 0, -1, f.seq, f.srcaddr);
    }

}


/*******************************************************************************
*                                TIMEOUT EVENTS                                *
*******************************************************************************/
static EVENT_HANDLER(timeouts)
{
    // If a timeout occurs, re-transmit the frame with the same parameters
    printf("timeout, ackexpect=%d\n", conn.ackexpected);
    transmit_frame(conn.lastmsg, conn.lastlength, conn.ackexpected, -1, conn.other_address);
}


/*******************************************************************************
*                         SHOW STATS (DEBUG BUTTON 0)                          *
*******************************************************************************/
static EVENT_HANDLER(showstate)
{
    printf(
    "\n\tconn.ackexpected\t= %d\n\tconn.nextframetosend\t= %d\n\tconn.frameexpected\t= %d\n",
		    conn.ackexpected, conn.nextframetosend, conn.frameexpected);
}

/*******************************************************************************
*                                  REBOOT NODE                                 *
*******************************************************************************/
EVENT_HANDLER(reboot_node)
{
    if(nodeinfo.nodenumber > 2) {
        fprintf(stderr,"This is not a 2-node network!\n");
        exit(1);
    }

    // Initialize the connection
    // This only works for two adjacent hosts, as required for part 2
    conn.lastlength = 0;
    conn.lasttimer = NULLTIMER;
    conn.ackexpected = 0;
    conn.nextframetosend = 0;
    conn.frameexpected = 0;
    conn.lastmsg	= calloc(1, sizeof(MSG));

    CHECK(CNET_set_handler( EV_APPLICATIONREADY, application_ready, 0));
    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
    CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));
    CHECK(CNET_set_handler( EV_DEBUG0,           showstate, 0));

    CHECK(CNET_set_debug_string( EV_DEBUG0, "State"));

    // Send and receive by both hosts
    //if(nodeinfo.nodenumber == 0)
	CNET_enable_application(ALLNODES);


}
