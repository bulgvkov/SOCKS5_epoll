#include "socks5.h"


void Sock5Server::ConnectEventHandle(int connectfd) //подключение
{
    TraceLog("new connect event:%d",connectfd);
    SetNonblocking(connectfd);
    OPEvent(connectfd,EPOLLIN,EPOLL_CTL_ADD);

    Connect* con = new Connect;
    con->_state = AUTH;
    con->_clientChannel._fd = connectfd;
    _fdConnectMap[connectfd] = con;
    con->_ref++;
}

int Sock5Server::AuthHandle(int fd) //обработка приветствия от клиента
{
    char buf[3]; //версия socks, количество методов, номера методово аутентификации
    int rlen = recv(fd,buf,3,MSG_PEEK);

    if(rlen <= 0)
    {
        return -1;
    }
    else if(rlen < 3)
    {
        return 0;
    }
    else
    {
        recv(fd,buf,rlen,0);
        if(buf[0] != 0X05)
        {
            ErrorLog("not socks5");
            return -1;
        }
        return 1;
    }
}

int Sock5Server::EstablishmentHandle(int fd) //обработка второго сообщения и подключение по настройкам клиента
{
    char buf[256];
    int rlen = recv(fd,buf,256,MSG_PEEK);
    TraceLog("Establishment recv:%d",rlen);
    if(rlen <= 0)
    {
        return -1;
    }
    else if(rlen < 10)
    {
        return -2;
    }
    else
    {
        char ip[4];
        char port[2];

        recv(fd,buf,4,0);
        char addresstype = buf[3];
        if(addresstype == 0x01)  //ipv4
        {
            TraceLog("use ipv4");
            recv(fd,ip,4,0);
            recv(fd,port,2,0);
        }
        else if(addresstype == 0x03) //domainname
        {
            char len = 0;
            recv(fd,&len,1,0);
            recv(fd,buf,len,0);

            buf[len] = '\0';
            TraceLog("encry domainname:%s",buf);

            recv(fd,port,2,0);


            TraceLog("decrypt domainname:%s",buf);
            struct hostent* hostptr = gethostbyname(buf);
            memcpy(ip,hostptr->h_addr,hostptr->h_length);
            TraceLog("domainname, use DNS success get ip");
        }
        else if(addresstype == 0x04)
        {
            ErrorLog("not support ipv6");
            return -1;
        }
        else
        {
            ErrorLog("invalid address type");
            return -1;
        }
        struct sockaddr_in addr;
        memset(&addr,0,sizeof(struct sockaddr_in));
        addr.sin_family = AF_INET;
        memcpy(&addr.sin_addr.s_addr,ip,4);
        addr.sin_port = *((uint16_t*)port);

        int serverfd = socket(AF_INET,SOCK_STREAM,0);
        if(serverfd < 0)
        {
            ErrorLog("server socket");
            return -1;
        }
        if(connect(serverfd,(struct sockaddr*)&addr,sizeof(addr)) < 0)
        {
            ErrorLog("connect error");
            close(serverfd);
            return -1;
        }
        TraceLog("Establishment success");
        return serverfd;
    }
}



void Sock5Server::ReadEventHandle(int connectfd) //чтение в зависимости от текущего состояния дескриптора
{
    TraceLog("read event:%d",connectfd);
    map<int,Connect*>::iterator it = _fdConnectMap.find(connectfd);
    if(it != _fdConnectMap.end())
    {
        Connect* con = it->second;

        if(con->_state == AUTH)
        {
            char replay[2];
            replay[0] = 0x05;
            int ret = AuthHandle(connectfd);
            if(ret == 0)
            {
                return;
            }
            else if(ret == 1)
            {
                replay[1] = 0x00;
                con->_state = ESTABLISHMENT;
            }
            else if(ret == -1)
            {
                replay[1] = 0xFF;
                RemoveConnect(connectfd);
            }

            if(send(connectfd,replay,2,0) != 2)
            {
                ErrorLog("auth replay");
            }
        }
        else if(con->_state == ESTABLISHMENT)
        {
            char replay[10] = {0};
            replay[0] = 0x05;

            int serverfd = EstablishmentHandle(connectfd);
            if(serverfd == -1)
            {
                replay[1] = 0x01;
                RemoveConnect(connectfd);
            }
            else if(serverfd == -2)
            {
                return;
            }
            else
            {
                replay[1] = 0x00;
                replay[3] = 0x01;
            }

            if(send(connectfd,replay,10,0) != 10)
            {
                ErrorLog("Establishment replay");
            }

            if(serverfd >= 0)
            {
                SetNonblocking(serverfd);
                OPEvent(serverfd,EPOLLIN,EPOLL_CTL_ADD);

                con->_serverChannel._fd = serverfd;
                _fdConnectMap[serverfd] = con;
                con->_ref++;
                con->_state = FORWARDING;
            }
        }
        else if(con->_state == FORWARDING)
        {
            Channel* clientChanne = &con->_clientChannel;
            Channel* serverChannel = &con->_serverChannel;

            bool sendencry = false,recvdecrypt = true;

            if(connectfd == serverChannel->_fd)
            {
                swap(clientChanne,serverChannel);
                swap(sendencry,recvdecrypt);
            }
            // client -> server
            Forwarding(clientChanne,serverChannel,sendencry,recvdecrypt);

        }
        else
        {
            assert(false);
        }
    }
    else
    {
        assert(false);
    }
}



int main()
{
    Sock5Server server(8000);
    server.Start();
    return 0;
}


