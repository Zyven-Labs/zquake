#ifndef SDL_NET_STUB_H
#define SDL_NET_STUB_H

typedef struct SDLNet_UDPSocket SDLNet_UDPSocket;
typedef struct SDLNet_UDPPacket SDLNet_UDPPacket;

static inline SDLNet_UDPSocket* SDLNet_UDPOpen(uint16_t port) { return nullptr; }
static inline int SDLNet_UDPRecv(SDLNet_UDPSocket* sock, SDLNet_UDPPacket* pkt) { return 0; }
static inline int SDLNet_UDPSend(SDLNet_UDPSocket* sock, SDLNet_UDPPacket* pkt) { return 0; }
static inline void SDLNet_UDPClose(SDLNet_UDPSocket* sock) {}
static inline SDLNet_UDPPacket* SDLNet_AllocPacket(int size) { return (SDLNet_UDPPacket*)0x1; }
static inline void SDLNet_FreePacket(SDLNet_UDPPacket* pkt) {}

static inline uint32_t SDLNet_Swap32(uint32_t x) { return x; }
static inline uint16_t SDLNet_Swap16(uint16_t x) { return x; }

struct SDLNet_IPAddress {
    uint32_t host;
    uint16_t port;
};

#endif
