
#if JN5164 == 5164      //TWELITE BLUE
#define DEFAULT_ADDRESS                     FALSE
#elif JN5169 == 5169    //TWELITE RED
#define DEFAULT_ADDRESS                     TRUE
#endif

#define DEFAULT_CHANNEL                     11
#define DEFAULT_TX_PROTECT_MODE             FALSE
#define DEFALUT_HW_FLOW_CONTROL             FALSE
#define DEFAULT_DEBUG_OUTPUT                TRUE
#define DEFAULT_DEBUG_LEVEL                 TRUE
#define DEFAULT_DEBUG_DEVICE                FALSE


#define BASE_ADDRESS                        0x200
#define BASE_CHANNEL                        11

#define PACKET_LENGTH_MAX                   104


#define PACKET_DATATYPE_MASK                6

#define PACKET_DATATYPE_CONNECTION_START    0
#define PACKET_DATATYPE_CONNECTION_REPLY    2
#define PACKET_DATATYPE_PROTECTMODE_BIT     1

#define PACKET_DATATYPE_DATA                4
#define PACKET_DATATYPE_PREVDATA_BIT        1


