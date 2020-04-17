#include <cnet.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
// Definitions for the FRAME size calculations
#define FRAME_HEADER_SIZE  (sizeof(FRAME) - sizeof(MSG))
#define FRAME_SIZE(f)      (FRAME_HEADER_SIZE + f.len)

// Assuming a max of 7 hosts
#define NH 7 

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
// A host will have one CONN for every other host
typedef struct{
    MSG       	*lastmsg;
    size_t		lastlength;
    CnetTimerID	lasttimer;
    CnetAddr    other_address;
    int       	ackexpected;
    int		    nextframetosend;
    int		    frameexpected;
    int         link;
} CONN;

// Initialize an array holding NH connections
// (Not all of these CONNs will necessarily be used, only one for every other host)
static CONN conn[NH];

// Count keeps track of how many CONNs we have added to our conn[] array
static int count = 0;

// The current CONN the host is trying to send a message over
static int current;


/*******************************************************************************
*                                TRANSMIT FRAME                                *
*******************************************************************************/
static void transmit_frame(MSG *msg, size_t length, int seqno, int ackno, CnetAddr destination, int forward, CnetAddr source)
{
    // Initialize a frame and a link
    FRAME       f;
    srand(time(NULL));
    int		link;

    // If we are forwarding a frame, forward it to the link provided in the argument.
    // Note that if we are forwarding, the frame preserves the original source address
    // If the destination location is unknown (i.e. -1) send the frame either left or right to explore
    // If we know the link required to reach a specific host (from previous acks), use that
    if(forward != -1){
        link = forward;
        f.seq       = seqno;
        f.ack       = ackno;
        f.checksum  = 0;
        f.len       = length;
        f.destaddr  = destination;
        f.srcaddr   = source;
    }
    else{
        if(conn[current].link == -1){
            if(nodeinfo.nlinks == 1){
                link = 1;
            }
            else{
                link = rand() % 2 + 1;
            }
        }
        else{
            link = conn[current].link;
        }
        f.seq       = seqno;
        f.ack       = ackno;
        f.checksum  = 0;
        f.len       = length;
        f.destaddr  = destination;
        f.srcaddr   = nodeinfo.address;
    }   

    // If an ack frame is being sent, not much to do
    // If a data frame is being sent (not forwarded!), start a timer
    if (f.ack > -1) {
        printf("ACK Sent: (src=%d, dest=%d, f.seq=%d f.ack=%d, msgLen=%d, link=%d)\n", f.srcaddr, f.destaddr, f.seq, f.ack, f.len, link);
    }
    else if(f.seq > -1){        
        printf("Data Sent: (src=%d, dest=%d, f.seq=%d, f.ack=%d, msgLen=%d, link=%d)\n", f.srcaddr, f.destaddr, f.seq, f.ack, f.len, link);
        if(forward==-1){
            CnetTime timeout;
            memcpy(&f.msg, msg, (int)length);
            timeout = FRAME_SIZE(f)*((CnetTime)8000000 / linkinfo[link].bandwidth) +
                        linkinfo[link].propagationdelay;
            conn[current].lasttimer = CNET_start_timer(EV_TIMER1, 3 * timeout, 0);
        }
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
    MSG *temp_msg = calloc(1, sizeof(MSG));
    size_t temp_len;
    temp_len = sizeof(MSG);

    // Store the read information into the initialized variables and disable sending further messages
    CHECK(CNET_read_application(&destaddr, temp_msg, &temp_len));
    CNET_disable_application(ALLNODES);

    // Check if this destination address has been added to our conn[] array, if not, add it.
    int done = 0;
    for(int i=0 ; i<NH ; i++){
        if(conn[i].other_address == destaddr){
            current = i;
            done = 1;
            break;
        }
    }
    if(done == 0){
        conn[count].other_address = destaddr;
        current = count;
        count++;
    }

    // Store a pointer to the message and length in the connection
    conn[current].lastmsg = temp_msg;
    conn[current].lastlength = temp_len;

    // Print notifiying that a new application layer message is being sent
    printf("down from application, ackexpect=%d, frameexpect=%d, nextframe=%d, dest=%d\n", 
        conn[current].ackexpected, conn[current].frameexpected, conn[current].nextframetosend, destaddr);

    // Call the transmit_frame function to complete processing before sending to physical layer
    transmit_frame(conn[current].lastmsg, conn[current].lastlength, conn[current].nextframetosend, -1, destaddr, -1, -1);
    conn[current].nextframetosend++;
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
    len          = sizeof(FRAME);

    // Read the data that has arrived on the physical layer
    CHECK(CNET_read_physical(&link, &f, &len));
    
    // Perform the checksum, and return if a bad frame was sent
    checksum    = f.checksum;
    f.checksum  = 0;
    if(CNET_ccitt((unsigned char *)&f, (int)len) != checksum) {
        printf("\tBAD frame: checksums(stored=%d, computed=%d)\n", checksum, CNET_ccitt((unsigned char *)&f, (int)len));
        return;           // bad checksum, ignore frame
    }

    // If an ack has arrived check if the frame was intended for this address
    // If so, check if this is the ack we are expecting from that particular address (by checking with the appropriate CONN)
    // If it is the correct frame, stop the timer, increment the expected ack, and re-enable the application layer
    // If an ack has arrived for a different address, forward the frame over the opposite link from which it arrived
    // If it is our first time receiving an ack from as specific node, take note of its link number for future transmissions
    if (f.ack > -1) {
        if(nodeinfo.address == f.destaddr){
            int index;
            for(int i=0 ; i<NH ; i++){
                if(conn[i].other_address == f.srcaddr){
                    index = i;
                    break;
                }
            }
            if(f.ack == conn[index].ackexpected && nodeinfo.address == f.destaddr) {
                printf("\tACK received: (src=%d, dest=%d, f.seq=%d, f.ack=%d, msgLen=%d)\n", f.srcaddr, f.destaddr, f.seq, f.ack, f.len);
                CNET_stop_timer(conn[index].lasttimer);
                conn[index].ackexpected++;
                if(conn[index].link == -1){
                    conn[index].link = link;
                }
                CNET_enable_application(ALLNODES);
            }
        }        
        else{
            printf("\t\tACK NOT received, seq=%d, ack=%d, ackexpect=%d\n", f.seq, f.ack, conn[current].ackexpected);
            if(nodeinfo.nlinks>1){
                printf("Forwarding ACK\n");
                int temp_link = link==1 ? 2 : 1;
                transmit_frame(&f.msg, len, f.seq, f.ack, f.destaddr, temp_link, f.srcaddr);
            }
        }
    }
    // If a data frame has arrived check if it was intended for this address
    // If so, check if the source address is in the conn[] for this node. 
    // Add the address if not in conn[], else retrieve the conn
    // If this is the seq expected for this particular conn, increment the frameexpected
    // An ack is sent indicating that the sequence has been received
    // If the frame was not intended for this address, forward the frame over the opposite link from which it arrvied
    else if(f.seq > -1){        
        printf("\tDATA received: (src=%d, dest=%d, f.seq=%d, f.ack=%d, msgLen=%d)", f.srcaddr, f.destaddr, f.seq, f.ack, f.len);
        if(nodeinfo.address == f.destaddr) {
            int index;
            int done = 0;
            for(int i=0 ; i<NH ; i++){
                if(conn[i].other_address == f.srcaddr){
                    done = 1;
                    index = i;
                    break;
                }
            }
            if(done == 0){
                conn[count].other_address = f.srcaddr;
                index = count;
                count++;
            }
            if(f.seq == conn[index].frameexpected){
                printf(" up to application\n");
                len = f.len;
                CNET_write_application(&f.msg, &len);
                conn[index].frameexpected++;
            }
            else{
                printf("ignored\n");
            }
            transmit_frame(NULL, 0, -1, f.seq, f.srcaddr, -1, -1);
        }
        else{
            if(nodeinfo.nlinks>1){
                printf("\nForwarding DATA\n");
                int temp_link = link==1 ? 2 : 1;
                transmit_frame(&f.msg, len, f.seq, f.ack, f.destaddr, temp_link, f.srcaddr);
            }
        }
    }

}


/*******************************************************************************
*                                TIMEOUT EVENTS                                *
*******************************************************************************/
static EVENT_HANDLER(timeouts)
{
    // If a timeout occurs, re-transmit the frame with the same parameters
    printf("timeout, ackexpect=%d\n", conn[current].ackexpected);
    transmit_frame(conn[current].lastmsg, conn[current].lastlength, conn[current].ackexpected, -1, conn[current].other_address, -1, -1);
}


/*******************************************************************************
*                         SHOW STATS (DEBUG BUTTON 0)                          *
*******************************************************************************/
static EVENT_HANDLER(showstate)
{
    for(int i=0 ; i<NH ; i++){
        printf(
        "\nConn#:%d Addr=%d: ackexpected=%d nextframetosend=%d frameexpected=%d link=%d",
                i, conn[i].other_address, conn[i].ackexpected, conn[i].nextframetosend, conn[i].frameexpected, conn[i].link);
    }
}


/*******************************************************************************
*                                  REBOOT NODE                                 *
*******************************************************************************/
EVENT_HANDLER(reboot_node)
{
    // Initialize the conn[]
    for(int i=0 ; i<NH ; i++){
        conn[i].lastlength = 0;
        conn[i].lasttimer = NULLTIMER;
        conn[i].ackexpected = 0;
        conn[i].nextframetosend = 0;
        conn[i].frameexpected = 0;
        conn[i].other_address = -1;
        conn[i].link = -1;
        conn[i].lastmsg	= calloc(1, sizeof(MSG));
    }

    // Only enable the application layer for hosts (routers only have physical layers)
    if(nodeinfo.nodetype == NT_HOST){
        CHECK(CNET_set_handler( EV_APPLICATIONREADY, application_ready, 0));
    }

    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
    CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));
    CHECK(CNET_set_handler( EV_DEBUG0,           showstate, 0));
    CHECK(CNET_set_debug_string( EV_DEBUG0, "State"));

    // Enable the application layer for all hosts
    if(nodeinfo.nodetype == NT_HOST){
	    CNET_enable_application(ALLNODES);
    }

}
