#include <cnet.h>
#include <cnetsupport.h>
#include <string.h>
#include <time.h>

/************************************************************
*   Routing in MANETs with Anchor Nodes                     *
*                                                           *
*   Submitted by: Neel Kumar                                *
*                                                           *
*   The structure for this code is borrowed from            *  
*   georouting.c provided on eClass + CNET documentation    *
*                                                           *
*   No collaboration                                        *
*                                                           *
*************************************************************/

/*******************************************************************************
*                              GLOBAL DECLARATIONS                             *
*******************************************************************************/
// Define the speed of the mobiles, and the pause time after they reach their dest
#define	WALKING_SPEED		15.0
#define	PAUSE_TIME		    2

// If an anchor is within this distance, we will forward the message to the anchor
#define FORWARDING_DISTANCE 50

// Randomly generated time period to send next frame (for mobiles)
#define	TX_NEXT			(5000000 + CNET_rand()%5000000)

// Header for a frame
typedef struct {
    int			    dest;
    int			    src;
    CnetPosition	srcpos;	        // position of the source
    int			    length;		    // length of payload
    bool            retransmitted;  // true if frame has been retransmitted once
    bool            anchor_request; // true if we are requesting data from anchor
} WLAN_HEADER;

// Frame containing a header and payload
typedef struct {
    WLAN_HEADER		header;
    char		payload[2304];
} WLAN_FRAME;

// Shared memory variables for global statistics
static	int		        *stats		= NULL;

// Set to false if you don't want details and stuff printed...?
static	bool		    verbose		= true;

// An array containing mobile and anchor addresses
// We can store a maximum of 100 addresses
int mobile_addresses[100];
int anchor_addresses[100];

// Number of mobile and anchor nodes
int mobile_count;
int anchor_count;

// Contains frames to be forwared to a mobile when requested
// Used only for anchor nodes
WLAN_FRAME anchor_buffer[20];

// A list of anchor locations
// Used only by mobiles
CnetPosition anchor_locations[100];
int anchor_locations_count;

// Bool that controls if mobiles can ask anchors for data
bool can_i_ask;


/*******************************************************************************
*                              CALCULATE DISTANCE                              *
*******************************************************************************/
// Calculates the distance between two CnetPositions
static double distance(CnetPosition p0, CnetPosition p1)
{
    int	dx	= p1.x - p0.x;
    int	dy	= p1.y - p0.y;

    return sqrt(dx*dx + dy*dy);
}


/*******************************************************************************
*                                   ASK ANCHOR                                *
*******************************************************************************/
// Every 10 seconds, a mobile is allowed to ask an anchor for data
// This prevents mobiles from continually asking anchors for data when they're nearby
static EVENT_HANDLER(ask_anchor)
{
    can_i_ask = true;
    CNET_start_timer(EV_TIMER3, TX_NEXT, 0);
}


/*******************************************************************************
*                       REQUEST FRAMES FROM NEARBY ANCHOR                      *
*******************************************************************************/
void request_from_anchor(int anchor_address_to_request_from)
{
    // Initalize a frame
    // Send over WLAN link
    WLAN_FRAME	frame;
    int	link = 1;

    // The destination is the anchor we want data from
    frame.header.dest = anchor_address_to_request_from;
    
    // Assign other header values
    frame.header.retransmitted = false;
    frame.header.anchor_request = true;
    frame.header.src = nodeinfo.address;
    CnetPosition current_position;
    CHECK(CNET_get_position(&current_position, NULL));
    frame.header.srcpos	= current_position;

    // Generate a payload message and its length
    sprintf(frame.payload, "Pls give me data! From: %d", nodeinfo.address);
    frame.header.length	= strlen(frame.payload) + 1;	// send nul-byte too

    // Transmit the frame
    size_t len = sizeof(WLAN_HEADER) + frame.header.length;
    CHECK(CNET_write_physical_reliable(link, &frame, &len));

    // Print that the message was sent
    if(verbose) {
        fprintf(stdout, "mobile [%3d]: requesting from anchor [%d]\n", nodeinfo.address, anchor_address_to_request_from);
    }

}


/*******************************************************************************
*                            TRANSMIT FRAME (mobile)                           *
*******************************************************************************/
static EVENT_HANDLER(transmit)
{
    // Initalize a frame
    // Send over WLAN link
    WLAN_FRAME	frame;
    int	link = 1;

    // Find a random destination from our list of mobile addresses
    do {
	    frame.header.dest = mobile_addresses[CNET_rand() % 100];
    } 
    while (frame.header.dest == nodeinfo.address || frame.header.dest == 0);
    
    // Assign other header values
    frame.header.retransmitted = false;
    frame.header.anchor_request = false;
    frame.header.src = nodeinfo.address;

    CnetPosition current_position;
    CHECK(CNET_get_position(&current_position, NULL));
    frame.header.srcpos	= current_position;

    // Generate a payload message and its length
    sprintf(frame.payload, "hello from %d", nodeinfo.address);
    frame.header.length	= strlen(frame.payload) + 1;	// send nul-byte too

    // Transmit the frame
    size_t len = sizeof(WLAN_HEADER) + frame.header.length;
    CHECK(CNET_write_physical_reliable(link, &frame, &len));
    ++stats[0];

    // Print that the message was sent
    if(verbose) {
        fprintf(stdout, "mobile [%3d]: pkt transmitted (src=%d, dest=%d)\n", nodeinfo.address, frame.header.src, frame.header.dest);
    }

    // SCHEDULE OUR NEXT TRANSMISSION
    CNET_start_timer(EV_TIMER1, TX_NEXT, 0);
}


/*******************************************************************************
*                             RECEIVE FRAME (mobile)                           *
*******************************************************************************/
static EVENT_HANDLER(receive)
{
    // Initalize a frame to catch the received frame
    WLAN_FRAME	frame;
    size_t	len;
    int		link;

    // Read the frame
    len	= sizeof(frame);
    CHECK(CNET_read_physical(&link, &frame, &len));

    // If the frame is from an anchor, check if the mobile is aware of this anchor
    // If not, add this anchor to our list of anchors
    // Finally, since the anchor is near us (because we received a message from it), ask the anchor for data
    if(frame.header.src < 100){
        bool found = false;
        for(int i=0 ; i<100 ; i++){
            if(frame.header.srcpos.x == anchor_locations[i].x && frame.header.srcpos.y == anchor_locations[i].y){
                found = true;
                break;
            }
        }
        if(!found){
            anchor_locations[anchor_locations_count] = frame.header.srcpos;
            anchor_locations_count++;
        }
        if(can_i_ask == true){
            can_i_ask = false;
            request_from_anchor(frame.header.src);
        }

    }
    // If the frame is not from an anchor, check if we are the intended recipient
    // If so, print 'pkt received'
    // If not, check if the frame has already been forwared. If it hasn't, forward it to an anchor
    // The frame is only forwared if an anchor is within FORWARDING_DISTANCE meters from this mobile
    else{
        if(frame.header.dest == nodeinfo.address) {
            ++stats[1];
            if(verbose){
                //fprintf(stdout, "\tfor me!\n");
                fprintf(stdout, "mobile [%3d]: pkt received (src=%d, dest=%d)\n", nodeinfo.address, frame.header.src, frame.header.dest);
            }
        }
        else{
            if(verbose){
                //fprintf(stdout, "\tnot mine!\n");
            }
            if(frame.header.retransmitted == false && frame.header.anchor_request == false){
                frame.header.retransmitted = true;
                CnetPosition current_position;
                CHECK(CNET_get_position(&current_position, NULL));
                for(int i=0 ; i<100 ; i++){
                    if(distance(anchor_locations[i], current_position) < FORWARDING_DISTANCE){
                        CHECK(CNET_write_physical_reliable(link, &frame, &len));
                        fprintf(stdout, "mobile [%3d]: pkt relayed (src=%d, dest=%d)\n", nodeinfo.address, frame.header.src, frame.header.dest);
                        break;
                    }
                }
            }
        }
    }

}


/*******************************************************************************
*                       ANCHOR REPLYING TO MOBILE REQUEST                      *
*******************************************************************************/
// When an anchor receives a request from a mobile, it sends any data intended for that mobile that it has stored in its buffer
void anchor_download_reply(int address_of_mobile_requesting_data)
{
    // Loop through anchor's buffer list and check for the requesting mobile as a 'dest'
    for(int i=0 ; i<20 ; i++){
        if(anchor_buffer[i].header.dest == address_of_mobile_requesting_data){
            
            // Send the frame to the appropriate mobile
            WLAN_FRAME frame;
            frame = anchor_buffer[i];
            int link = 1;
            size_t len = sizeof(WLAN_HEADER) + frame.header.length;
            CHECK(CNET_write_physical_reliable(link, &frame, &len));
            fprintf(stdout, "anchor [%3d]: download reply (src=%d, dest=%d)\n", nodeinfo.address, frame.header.src, frame.header.dest);

            // Effectively 'clear' that frame from the buffer by making the src and dest zero
            anchor_buffer[i].header.dest = 0;
            anchor_buffer[i].header.src = 0;

        }
    }
}


/*******************************************************************************
*                             RECEIVE FRAME (anchor)                           *
*******************************************************************************/
static EVENT_HANDLER(receive_anchor){

    // Initalize a frame to catch the received frame
    WLAN_FRAME	frame;
    size_t	len;
    int		link;

    // Read the frame
    len	= sizeof(frame);
    CHECK(CNET_read_physical(&link, &frame, &len));
   
    // Check if the frame is meant for retransmission
    // If so, make sure it's not already in our buffer (two mobiles can relay the same frame)
    // If not in the anchor's buffer, add it to an empty slot in the buffer
    if(frame.header.retransmitted == true && frame.header.anchor_request == false){
        bool found = false;
        for(int i=0 ; i<20 ; i++){
            if(anchor_buffer[i].header.dest == frame.header.dest 
                && anchor_buffer[i].header.src == frame.header.src 
                && anchor_buffer[i].header.length == frame.header.length){
                    found = true;
                    break;
                }
        }
        if(found==false){
            for(int i=0 ; i<20 ; i++){
                if(anchor_buffer[i].header.dest == 0 && anchor_buffer[i].header.src == 0){
                    anchor_buffer[i] = frame;
                    if(verbose) {
                        double	rx_signal;
                        CHECK(CNET_wlan_arrival(link, &rx_signal, NULL));
                        fprintf(stdout, "anchor [%3d]: frame stored (src=%d, dest=%d)\t", nodeinfo.address, frame.header.src, frame.header.dest);
                        // Prints the buffer's capacity. A '1' indicates that slot is occupied, '0' indicates that slot is empty
                        fprintf(stdout,"Buffer Capacity: [");
                        for(int i=0 ; i<20 ; i++){
                            if(anchor_buffer[i].header.dest == 0 && anchor_buffer[i].header.src == 0){
                                fprintf(stdout,"0");
                            }
                            else{
                                fprintf(stdout,"1");
                            }
                        }
                        fprintf(stdout,"]\n");
                    }
                    break;
                }
            }
        }
    }
    // If the frame is a mobile asking an anchor for data, send that mobile any of its data that is stored in this buffer
    else if(frame.header.anchor_request == true && frame.header.dest == nodeinfo.address){
        fprintf(stdout, "anchor [%3d]: download request (src=%d, dest=%d)\n", nodeinfo.address, frame.header.src, frame.header.dest);
        anchor_download_reply(frame.header.src);
    }

}


/*******************************************************************************
*                            BROADCAST BEACON (anchor)                         *
*******************************************************************************/
// Initially, mobiles do not know the location of anchors
// This beacon goes off every second
// It serves the purpose of letting mobiles know the anchor location (when the program is starting up)
// Additionally, when mobiles hear this beacon, they know an anchor is nearby, so they request the data from this anchor
static EVENT_HANDLER(broadcast_beacon){

    // Initalize a frame
    WLAN_FRAME	frame;
    int		link	= 1;

    // There's no intended destination for this frame. It's a general signal to all mobiles
    frame.header.dest = 1000;
    frame.header.src = nodeinfo.address;

    // The position of this anchor is stored, so mobiles can extract/store this location
    CnetPosition anchor_position;
    CHECK(CNET_get_position(&anchor_position, NULL));
    frame.header.srcpos	= anchor_position;	// me!

    // Anchor payload not necessary, but we just say hi
    sprintf(frame.payload, "hello from anchor %d", nodeinfo.address);
    frame.header.length	= strlen(frame.payload) + 1;	// send nul-byte too
    size_t len	= sizeof(WLAN_HEADER) + frame.header.length;

    // TRANSMIT THE FRAME
    CHECK(CNET_write_physical_reliable(link, &frame, &len));

    // Send a beacon every second
    CNET_start_timer(EV_TIMER2, 1000000, 0);
}


/*******************************************************************************
*                              SUMMARY STATISTICS                              *
*******************************************************************************/
static EVENT_HANDLER(finished)
{
    fprintf(stdout, "messages generated:\t%d\n", stats[0]);
    fprintf(stdout, "messages received:\t%d\n", stats[1]);

    if(stats[0] > 0){
	    fprintf(stdout, "delivery ratio:\t\t%.1f%%\n", 100.0*stats[1]/stats[0]);
    }
}


/*******************************************************************************
*                                 PARSE STRING                                 *
*******************************************************************************/
// This function simply parses the string from the topology file
// It extracts the addresses and inserts them into global integer arrays
void parse_string(char* string, char type){

    char* token = strtok(string, ",");
    while(token != NULL){
        if(type == 'a'){
            anchor_addresses[anchor_count] = atoi(token);
            anchor_count++;
        }
        else if(type == 'm'){
            mobile_addresses[mobile_count] = atoi(token);
            mobile_count++;
        }
        token = strtok(NULL, ",");
    }

}


/*******************************************************************************
*                                  REBOOT NODE                                 *
*******************************************************************************/
EVENT_HANDLER(reboot_node)
{

    // Pull the string of addresses from the topology file
    // Extract the address from the string as integers and insert them into an array of integers
    memset(mobile_addresses, 0, 100);
    memset(anchor_addresses, 0, 100);
    char mobile_string[500];
    char anchor_string[500];
    strcpy(mobile_string, CNET_getvar("mobiles"));
    strcpy(anchor_string, CNET_getvar("anchors"));
    mobile_count = 0;
    anchor_count = 0;
    parse_string(mobile_string, 'm');
    parse_string(anchor_string, 'a');

    // Reboot sequence for an anchor
    if(nodeinfo.nodetype == NT_HOST){
        CHECK(CNET_set_handler(EV_PHYSICALREADY,  receive_anchor, 0));
        CHECK(CNET_set_handler(EV_TIMER2, broadcast_beacon, 0));
        CNET_start_timer(EV_TIMER2, 1000000, 0);

        // Clear our buffer
        for(int i=0 ; i<20 ; i++){
            anchor_buffer[i].header.dest = 0;
            anchor_buffer[i].header.src = 0;
        }
    }

    // Reboot sequence for a mobile
    if(nodeinfo.nodetype == NT_MOBILE){
        
        // Initially, we know the locations of no anchors
        anchor_locations_count = 0;
        // Initially, mobiles can ask for data from anchors
        can_i_ask = true;

        // Declar the external function?
        extern void init_mobility(double walkspeed_m_per_sec, int pausetime_secs, int nnodes);

        // Check for proper version of CNET
        CNET_check_version(CNET_VERSION);
        CNET_srand(time(NULL) + nodeinfo.nodenumber);

        // Call init_mobility to set up the mobile movements
        init_mobility(WALKING_SPEED, PAUSE_TIME, mobile_count);

        // ALLOCATE MEMORY FOR SHARED MEMORY SEGMENTS
        stats	= CNET_shmem2("s", 2*sizeof(int));

        // Set the event handles for mobiles
        // A TIMER1 event causes new transmissions
        // A TIMER3 event resets the 'request from anchor' to true
        CHECK(CNET_set_handler(EV_TIMER1, transmit, 0));
        CHECK(CNET_set_handler(EV_TIMER3, ask_anchor, 0));
        CNET_start_timer(EV_TIMER1, TX_NEXT, 0);
        CNET_start_timer(EV_TIMER3, 1000000, 0);

        // Set event handler for when a physical layer message is received
        CHECK(CNET_set_handler(EV_PHYSICALREADY,  receive, 0));

        // Print statistics when simulation ends
        if(nodeinfo.nodenumber == 0){
            CHECK(CNET_set_handler(EV_SHUTDOWN,  finished, 0));
        }
    }

}
