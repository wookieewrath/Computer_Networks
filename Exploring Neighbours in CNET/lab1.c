
/************************************************************
*   Submitted by: Neel Kumar                                *
*                                                           *
*   The structure for this code is borrowed from the        *  
*   explore-w20.c file on eClass.                           *
*                                                           *
*   Any additions to the original code are                  *
*   commented throughout.                                   *
*************************************************************/

#include <cnet.h>
#include <assert.h>
#include <string.h>


/*******************************************************************************
*                                GLOBAL VARIABLES                              *
*******************************************************************************/
typedef enum { EXPLORE, EXPLORE_ACK } FRAMEKIND; // Changed from 'HELLO' to 'EXPLORE'...

typedef struct {
  char         data[MAX_NODENAME_LEN];
} MSG;

// The FRAME struct was modified to carry additional information such as:
// name, address, bandwidth, and number of neighboring nodes.
typedef struct {
  FRAMEKIND    kind;
  CnetAddr     srcAddr;
  CnetTime     time_sent;
  MSG          msg;
  char         neighbour_name[MAX_NODENAME_LEN];
  int          neighbour_address;
  int          neighbour_bandwidth;
  int          neighbour_num_nodes;
} FRAME;

// This global struct was added to keep track of all the neighbour information
struct our_neighbours{
  int       the_num_links;
  char      list_struct_names[MAX_NODENAME_LEN];
  int       list_struct_address;
  int       list_struct_bandwidth;
  int       list_struct_num_nodes;
};

// This is a global list containing the "our_neighbours" struct. 
// It assumes that a node does not have more than 32 neighbours
struct our_neighbours neighbour_list[32];

// Two additional global variables to keep track of the
// number of sent and received EXPLORE messages
int       sent=0;
int       received=0;


/*******************************************************************************
*                                BUTTON PRESSED                                *
*******************************************************************************/
// The button pressed event was modified to print the additional data
static EVENT_HANDLER(button_pressed)
{
    printf("\n NOTE: If the table is showing null values,\n briefly wait and try again.\n The nodes need time to exchange data.\n\n");
    printf(" Node name       : %s\n",	nodeinfo.nodename);
    printf(" Node number     : %d\n",	nodeinfo.nodenumber);
    printf(" Node address    : %d\n",	nodeinfo.address);
    printf(" Number of links : %d\n",	nodeinfo.nlinks);
    printf(" Total # of EXPLORE messages transmitted: %d\n", sent);             // Prints number of transmitted messages
    printf(" Total # of EXPLORE_ACK messages received: %d\n", received);        // Prints number of received messages
    for(int i=1 ; i<nodeinfo.nlinks+1 ; i++){                                   // This for loop iterates through the number of neighbours and prints the required data
        printf(" [%d] %s     (%d), #links= %d, bandwidth= %d bps\n", 
        i, 
        neighbour_list[i].list_struct_names, 
        neighbour_list[i].list_struct_address, 
        neighbour_list[i].list_struct_num_nodes, 
        neighbour_list[i].list_struct_bandwidth
        );
    }
    printf("\n");
}


/*******************************************************************************
*                             MANAGING PHYSICAL LAYER                          *
*******************************************************************************/
static EVENT_HANDLER(physical_ready)
{

  int       link;
  size_t    len;
  // CnetTime  alpha;     // Not sure if still required.
  FRAME     f;

  len= sizeof(FRAME);
  CHECK ( CNET_read_physical (&link, (char *) &f, &len) );

  switch (f.kind) {
  case EXPLORE:
      // The line below was modified to assert there are 32 or fewer nodes
      assert ( link <= 32 );
      f.kind = EXPLORE_ACK;

      // The following five lines were added to transmit the following data to the receiving node:
      sent++;                                                         // Increment the number of sent messages
      memcpy(f.neighbour_name, nodeinfo.nodename, MAX_NODENAME_LEN);  // The node sends its name
      f.neighbour_address = nodeinfo.address;                         // ..and address
      f.neighbour_num_nodes = nodeinfo.nlinks;                        // ..and number of neighbors
      f.neighbour_bandwidth = linkinfo[1].bandwidth;                  // ..and bandwidth

      len= sizeof(f);
      CHECK( CNET_write_physical(link, (char *) &f, &len) );
      break;


  case EXPLORE_ACK:     
      
      // The following five lines were added. These take the received message, and update the current node's "neighbour table":
      received++;                                                                          // Increment the number of received messages
      memcpy(neighbour_list[link].list_struct_names, f.neighbour_name, MAX_NODENAME_LEN);  // Add the neighbor's name
      neighbour_list[link].list_struct_address = f.neighbour_address;                      // ..and the neighbor's address
      neighbour_list[link].list_struct_num_nodes = f.neighbour_num_nodes;                  // ..and the neighbor's number of neighbors
      neighbour_list[link].list_struct_bandwidth = f.neighbour_bandwidth;                  // ..and the neighbor's bandwidth


      // These were included in the EXPLORE-W20 file. Not sure if still required. It has been commented out for now...
      //assert ( f.srcAddr == nodeinfo.address );
      //alpha= (nodeinfo.time_in_usec - f.time_sent) / 2;
      //printf("debug: alpha= %lld\n", alpha);

      break;
  } 
}


/*******************************************************************************
*                               SENDING 'EXPLORE's                             *
*******************************************************************************/
static EVENT_HANDLER(send_EXPLORE)
{
  
  // This FOR loop was added. It ensures that the current node is sending 
  // EXPLORE messages to ALL neighbors.
  for(int i=0 ; i<nodeinfo.nlinks ; i++)
  {
    int     link = i+1; // Iterate and send to all links!!!
    size_t  len;
    FRAME   f;

    f.kind = EXPLORE;
    f.srcAddr = nodeinfo.address;
    f.time_sent = nodeinfo.time_in_usec;

    len= sizeof(f);
    CHECK( CNET_write_physical(link, (char *) &f, &len) );
  }

  CNET_start_timer (EV_TIMER1, 100000, 0);
}


/*******************************************************************************
*                                INITIALIZE LOOP                               *
*******************************************************************************/
// No changes were made here
EVENT_HANDLER(reboot_node)
{

  CNET_set_handler(EV_DEBUG0, button_pressed, 0);
  CNET_set_debug_string(EV_DEBUG0, "Node Info!"); 

  CNET_set_handler(EV_PHYSICALREADY, physical_ready, 0);
  CNET_set_handler(EV_TIMER1, send_EXPLORE, 0);

  CNET_start_timer (EV_TIMER1, 100000, 0);

}
