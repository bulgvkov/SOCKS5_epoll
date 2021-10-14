#ifndef __EPOLL_H__ 
#define __EPOLL_H__

#include "common.h"

class IgnoreSigPipe
{
public:
    IgnoreSigPipe()
    {
        ::signal(SIGPIPE,SIG_IGN);
    }
};

static IgnoreSigPipe initPIPE_IGN;

class EpollServer
{
public:
    EpollServer(int port)
        : _port(port)
        , _listenfd(-1)
        , _eventfd(-1)
    {}

    virtual ~EpollServer()
    {
        if(_listenfd)
            close(_listenfd);
    }

    void OPEvent(int fd,int events,int op) //установка на описателе настроек, которые мне необходимы
    {
        struct epoll_event event;
        event.events = events;
        event.data.fd = fd;
        if(epoll_ctl(_eventfd,op,fd,&event) < 0)
        {
            ErrorLog("epoll_ctl(op:%d,fd:%d)",op,fd);
        }
    }
    
    void SetNonblocking(int sfd) //установка флага на сокете, что операции не будут блокирующие
    {
        int flags;

        flags = fcntl(sfd,F_GETFL,0);
        if(flags == -1)
            ErrorLog("SetNonblocking:F_GETFL");

        flags |= O_NONBLOCK;
        if(fcntl(sfd,F_GETFL,flags))
            ErrorLog("SetNonblocking:F_GETFL");
    }

    enum Sock5State
    {
        AUTH,
        ESTABLISHMENT,
        FORWARDING,
    };

    struct Channel //файловый дискриптор и буффер
    {
        int _fd;
        string _buff;

        Channel()
            : _fd(-1)
        {}
    };

    struct Connect //Описание соединения: состояние соединения, файловый дискриптор, буффер
    {
        Sock5State _state;
        Channel _clientChannel;
        Channel _serverChannel;
        int _ref;

        Connect()
            :_state(AUTH)
            ,_ref(0)
        {}

    };


    void Start();

    [[noreturn]] void EventLoop();

    void SendInLoop(int fd, const char* buf,int len);
    void Forwarding(Channel* clientChannel,Channel* serverChannel,bool sendencry,bool recvdecrypt);
    void RemoveConnect(int fd); 

    virtual void ConnectEventHandle(int connectfd) = 0;
    virtual void ReadEventHandle(int connectfd) = 0;
    virtual void WriteEventHandle(int connectfd);

protected:
    int _port;
    int _listenfd;
    int _eventfd;
    
    map<int,Connect*> _fdConnectMap; //контейнер с текущими сессиями
};

#endif 


