#include "tpkt.h"
#include "tcp.h"

#include <assert.h>
#include <stdlib.h>
#include <arpa/inet.h>

struct tpkt_dev_t
{
    ppkt_receive_function_t receive;
    struct tcp_dev_t  *tcp;
    void *user;

    struct ppkt_t *pktqueue;
};

struct tpkthdr_t
{
    uint8_t version;
    uint8_t reserved;
    uint16_t size;
} __attribute__((packed));

static err_t tpkt_receive(struct ppkt_t *p, void *user)
{
    assert(p);

    struct tpkt_dev_t *dev = (struct tpkt_dev_t*)user;
    assert(dev);

    dev->pktqueue = ppkt_append_footer(p, dev->pktqueue);

    while (ppkt_chain_size(dev->pktqueue) >= sizeof(struct tpkthdr_t))
    {
        dev->pktqueue = ppkt_coalesce(dev->pktqueue, sizeof(struct tpkthdr_t));

        struct tpkthdr_t *tpkthdr = PPKT_GET(struct tpkthdr_t, dev->pktqueue);

        assert(tpkthdr->version == 3);

        size_t tpkt_size = ntohs(tpkthdr->size);
        if (ppkt_chain_size(dev->pktqueue) < tpkt_size)
            // Need more data
            return ERR_NONE;

        // Split the chain into before and after
        struct ppkt_t *pkt = dev->pktqueue;
        ppkt_split(pkt, &dev->pktqueue, tpkt_size);

        // Skip header in the arrived packet
        ppkt_pull(pkt, sizeof(struct tpkthdr_t));

        // Pass the result to our upper layer
        assert(dev->receive);
        err_t ret = dev->receive(pkt, dev->user);
        if (ret != ERR_NONE)
            return ret;
    }

    return ERR_NONE;
}

struct tpkt_dev_t* tpkt_connect(const char *addr, ppkt_receive_function_t receive, void *user)
{
    assert(addr);
    assert(receive);

    struct tpkt_dev_t *dev = malloc(sizeof(struct tpkt_dev_t));
    if (! dev)
        return NULL;

    dev->tcp = NULL;
    dev->receive = receive;
    dev->user = user;
    dev->pktqueue = NULL;

    dev->tcp = tcp_connect(addr, tpkt_receive, dev);

    if (! dev->tcp)
    {
        free(dev);
        dev = NULL;
    }

    return dev;
}

void tpkt_disconnect(struct tpkt_dev_t *dev)
{
    assert(dev);
    assert(dev->tcp);

    tcp_disconnect(dev->tcp);

    ppkt_free(dev->pktqueue);
    free(dev);
}

err_t tpkt_send(struct tpkt_dev_t *dev, struct ppkt_t *p)
{
    assert(dev);
    assert(p);

    struct ppkt_t *hdr = ppkt_alloc(sizeof(struct tpkthdr_t));
    struct tpkthdr_t *tpkthdr = PPKT_GET(struct tpkthdr_t, hdr);

    tpkthdr->version = 3;
    tpkthdr->reserved = 0;
    tpkthdr->size = htons(ppkt_chain_size(p) + sizeof(struct tpkthdr_t));

    p = ppkt_prefix_header(hdr, p);

    assert(dev->tcp);
    return tcp_send(dev->tcp, p);
}

err_t tpkt_poll(struct tpkt_dev_t *dev)
{
    assert(dev);
    return tcp_poll(dev->tcp);
}
