#include "epoll.h"

void EpollServer::Start() //конфигурация соединения и установка сокета в готовность принимать входящие соединения
{
    _listenfd = socket(PF_INET,SOCK_STREAM,0);
    if(_listenfd == -1)
    {
        ErrorLog("create socket");
        return;
    }

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(_listenfd,(struct sockaddr*)&addr,sizeof(addr)) < 0)
    {
        ErrorLog("bind error");
        return;
    }
    
    if(listen(_listenfd,100000) < 0)
    {
        ErrorLog("listen");
        return;
    }

    TraceLog("epoll server listen on %d",_port);
    
    _eventfd = epoll_create(100000);
    if(_eventfd == -1)
    {
        ErrorLog("epoll_create");
        return;
    }

    SetNonblocking(_listenfd);
    OPEvent(_listenfd, EPOLLIN, EPOLL_CTL_ADD);

    EventLoop();
}

void EpollServer::EventLoop()   //синхронный демультиплексер событий - бесконечный цикл, при готовности свободного сокета получает "задание", что делать
{
    struct epoll_event events[100000];
    while(1)
    {
        int n = epoll_wait(_eventfd, events, 100000,0);
        for(int i = 0;i < n; ++i)
        {
            if(events[i].data.fd == _listenfd)
            {
                struct sockaddr clientaddr;
                socklen_t len;
                int connectfd = accept(_listenfd, &clientaddr, &len);
                if(connectfd < 0)
                    ErrorLog("accept");

                TraceLog("new connect...");
                ConnectEventHandle(connectfd);
            }
            else if(events[i].events & EPOLLIN)
                ReadEventHandle(events[i].data.fd);
            else if(events[i].events & EPOLLOUT)
                WriteEventHandle(events[i].data.fd);
            else 
                ErrorLog("event %d",events[i].data.fd);

        }
    }
}


void EpollServer::Forwarding(Channel* clientChannel,Channel* serverChannel,bool sender,bool receiver) //чтение информации с клиентского сокета в буффер
{
    char buf[4096];
    int rlen = recv(clientChannel->_fd,buf,4096,0);
    signal(SIGPIPE,SIG_IGN); //игнорирование сигнала SIGPIPE - чтобы не умер сервер, если клиент отсоединился
    if(rlen < 0)
    {
        ErrorLog("recv : %d",clientChannel->_fd);
    }
    else if(rlen == 0)
    {
        shutdown(serverChannel->_fd,SHUT_WR);
        RemoveConnect(clientChannel->_fd);
    }
    else 
    {
        buf[rlen] = '\0';
        SendInLoop(serverChannel->_fd,buf,rlen);
    }
}


void EpollServer::SendInLoop(int fd, const char* buf,int len) //занести в информацию epoll'а, что нужно повторить запись
{
    int slen = send(fd,buf,len,0);
    if(slen < 0)
    {
        ErrorLog("send to %d",fd);
    }
    else if(slen < len)
    {
        TraceLog("recv %d bytes,send %d bytes,left %d send in loop",len,slen,len-slen);
        map<int,Connect*>::iterator it = _fdConnectMap.find(fd);
        if(it != _fdConnectMap.end())
        {
            Connect* con = it->second;
            Channel* channel = &con->_clientChannel;
            if(fd == con->_serverChannel._fd)
            {
                channel = &con->_serverChannel;
            }
            int events = EPOLLOUT | EPOLLIN | EPOLLONESHOT;
            OPEvent(fd,events,EPOLL_CTL_MOD);

            channel->_buff.append(buf+slen);
        }
        else 
        {
            assert(false);
        }

    }
}



void EpollServer::RemoveConnect(int fd) //удалить соединение
{
    OPEvent(fd,0,EPOLL_CTL_DEL);
    map<int,Connect*>::iterator it = _fdConnectMap.find(fd);
    if(it != _fdConnectMap.end())
    {
        Connect* con = it->second;
        if(--con->_ref == 0)
        {
            delete con;
            _fdConnectMap.erase(it);
        }
    }
    else 
    {
        assert(false);
    }
}


void EpollServer::WriteEventHandle(int fd) //запись
{
    map<int,Connect*>::iterator it = _fdConnectMap.find(fd);
    if(it != _fdConnectMap.end())
    {
        Connect* con = it->second;
        Channel* channel = &con->_clientChannel;
        if(fd == con->_serverChannel._fd)
        {
            channel = &con->_serverChannel;
        }
        string buff;
        buff.swap(channel->_buff);
        SendInLoop(fd,buff.c_str(),buff.size());
    }
    else 
    {
        assert(fd);
    }
}
