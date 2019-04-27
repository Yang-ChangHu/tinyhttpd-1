#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <queue>
#include <string>
#include <map>

using namespace std;

#define SERVER_STRING "Server: httpd++/1.0.0\r\n"
#define ERROR_DIE(msg)  do {            \
        perror("[ERROR_DIE]"#msg);       \
        exit(1);                        \
    } while(0)

#define ERROR(msg)      do {            \
        perror("[ERROR]"#msg);           \
    } while(0)

#ifdef DEBUG
#define LOG(fmt, ...)   fprintf(stdout, "[%s][%u]" fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define LOG(fmt, ...)
#endif

#define MAX_BUF_SIZE 1024

class Httpd;

class HttpdSocket
{
public:
    HttpdSocket() : m_client_fd_(0), m_buffer_xi_(0), 
        m_query_(NULL), m_buffer_len_(0), m_httpd_(NULL)
    { }

    HttpdSocket(int fd, struct sockaddr_in &s) :
        m_client_fd_(fd), m_client_name_(s), m_buffer_xi_(0), 
        m_query_(NULL), m_buffer_len_(0), m_httpd_(NULL)
    { }

    virtual ~HttpdSocket()
    {
        reset();
    }

    inline void setClientFd(int fd)
    {
        m_client_fd_ = fd;
    }

    inline void setClientName(struct sockaddr_in &client)
    {
        m_client_name_ = client;
    }

    inline void setHttpd(Httpd *h)
    {
        m_httpd_ = h;
    }

    inline Httpd* getHttpd()
    {
        return m_httpd_;
    }

    inline void close()
    {
        if (m_client_fd_ > 0) 
        {
            ::close(m_client_fd_);
        }
    }

    inline void reset()
    {
        m_client_fd_ = 0;
        m_buffer_xi_ = 0; 
        m_query_ = NULL;
        m_buffer_len_ = 0;
        m_httpd_ = NULL;
    }

    // 解析方法
    inline void parseMethod();

    // 解析query
    inline void parseUrl();

    // 解析header
    inline void parseHeader();

    // 获取content-length
    inline int getContentLength()
    {
        map<string, string>::iterator iter = m_header_.find("content-length");
        if (iter != m_header_.end())
        {
            LOG("content-length:%s", (iter->second).c_str());
            return atoi((iter->second).c_str());
        }

        return 0;
    }

    inline bool isPOST()
    {
        if (strcasecmp(m_method_, "POST") == 0)
        {
            return true;
        }

        return false;
    }

    inline bool isGET()
    {
        if (strcasecmp(m_method_, "GET") == 0)
        {
            return true;
        }

        return false;
    }

    inline char* getUrl()
    {
        return m_url_;
    }

    virtual bool cgi()
    {
        if (strstr(m_url_, ".cgi") == NULL)
        {
            return false;
        }

        return true;
    }

    virtual void error501();

    virtual void error500();

    virtual void error404();

    virtual void error400();

    virtual void serveFile(const char *path);

    virtual void executeCGI(const char *path);
    
    int discardBody()
    {
        int len = 0, read_len = 0;
        while ((len > 0) && strcmp("\n", m_buffer_))
        {
            len = getLine();
            read_len += len;
        }
        m_buffer_xi_ = 0;

        return read_len;
    }

    int split(const char* str, string &key, string &value)
    {
        int xi = 0;
        while (str[xi] != '\0' && isspace(str[xi])) xi++;

        // 先处理key
        while (str[xi] != '\0' && str[xi] != ':' && str[xi] != '\n')
        {
            key = key + (char)tolower(str[xi]);
            xi++;
        }

        if (str[xi] == ':' || str[xi] == '\n')
        {
            xi++;
        }

        while (str[xi] != '\0' && isspace(str[xi])) xi++;

        while (str[xi] != '\0' && str[xi] != '\n')
        {
            value = value + (char)tolower(str[xi]);
            xi++;
        }

        return xi + 1;
    }

    // 每次获取一行的数据
    int getLine()
    {
        int i = 0, n = 0;
        char c = '\0';

        while ((i < MAX_BUF_SIZE - 1) && (c != '\n'))
        {
            n = ::recv(m_client_fd_, &c, 1, 0);
            if (n > 0)
            {
                if (c == '\r')
                {
                    n = ::recv(m_client_fd_, &c, 1, MSG_PEEK);
                    if ((n > 0) && (c == '\n'))
                    {
                        ::recv(m_client_fd_, &c, 1, 0);
                    }
                    else
                    {
                        c = '\n';
                    }
                }
                m_buffer_[i] = c;
                i++;
            }
            else
            {
                c = '\n';
            }
        }
        m_buffer_[i] = '\0';
        // 记录当前的buf的大小
        m_buffer_len_ = i;
        LOG("read line:%s", m_buffer_);

        return i;
    }
    
private:
    int m_client_fd_;
    struct sockaddr_in m_client_name_;
    char *m_query_;
    size_t m_buffer_xi_, m_buffer_len_;
    char m_buffer_[MAX_BUF_SIZE], m_method_[255], m_url_[255];

    map<string, string> m_header_;
    Httpd *m_httpd_;
};

typedef HttpdSocket* HttpdSocketPtr;

class Httpd
{
public:
    Httpd() : m_socket_fd_(0)
    { }

    ~Httpd()
    {
        // 清空m_queue_
        while (m_queue_.empty())
        {
            HttpdSocketPtr o = m_queue_.front();
            m_queue_.pop();
            delete o;
        }
    }

    void startup(u_short port);

    void loop();

    HttpdSocketPtr newObject()
    {
        if (m_queue_.empty()) 
        {
            m_queue_.push(new HttpdSocket());
        }

        HttpdSocketPtr o = m_queue_.front();
        m_queue_.pop();

        LOG("object:%p", o);

        return o;
    }

    void freeObject(HttpdSocketPtr o)
    {
        LOG("object:%p", o);

        if (o != NULL)
        {
            o->reset(); // 重置
            m_queue_.push(o);
        }
    }

private:
    int m_socket_fd_;
    queue<HttpdSocketPtr> m_queue_;
};