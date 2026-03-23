#ifndef ETH_REQ_H
#define ETH_REQ_H   

#define ID_LEN                      8  

typedef struct {
    char id_message[ID_LEN+1]; //TODO: should I just make ID_LEN include null terminator? i could go either way here
} scan_buffer_received;  

void init_eth(void);
int http_get_task(const scan_buffer_received input);   


                                        
#endif    