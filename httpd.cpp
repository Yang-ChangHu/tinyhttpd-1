#include <httpd.h>

static void* accept_request(void *arg)
{
    struct stat st;
    HttpdSocketPtr socket = (HttpdSocketPtr)arg;
    socket->parseMethod();
    if (!socket->isPOST() && !socket->isGET())
    {
        socket->error501();
        return NULL;
    }
    // 解析URL
    socket->parseUrl();
    // 解析header
    socket->parseHeader();
    LOG("content-length:%d", socket->getContentLength());

    char path[512];
    snprintf(path, sizeof(path) - 1, "htdocs%s", socket->getUrl());
    if (path[strlen(path) - 1] == '/')
    {
        strcat(path, "index.html");
    }
    // 读取文件失败
    if (stat(path, &st) == -1) 
    {
        socket->discardBody();
        socket->error404();
    }
    else
    {
        if ((st.st_mode & S_IFMT) == S_IFDIR)
        {
            strcat(path, "/index.html");
        }
        LOG("cgi:%d, path:%s", socket->cgi(), path);
        // 不采用cgi
        if (!(socket->cgi()))
        {
            socket->serveFile(path);
        }
        else
        {
            socket->executeCGI(path);
        }
    }
    socket->close();
    // 释放对象
    socket->getHttpd()->freeObject(socket);
    return NULL;
}

void Httpd::startup(u_short port)
{
    struct sockaddr_in name;

    m_socket_fd_ = socket(PF_INET, SOCK_STREAM, 0);
    if (m_socket_fd_ == -1)
    {
        ERROR_DIE("socket");
    }
    
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(m_socket_fd_, (struct sockaddr *)&name, sizeof(name)) < 0)
    {
        ERROR_DIE("bind");
    }
    
    if (port == 0)
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(m_socket_fd_, (struct sockaddr *)&name, &namelen) == -1)
        {
            ERROR_DIE("getsockname");
        }
        port = ntohs(name.sin_port);
    }

    if (listen(m_socket_fd_, 5) < 0)
    {
        ERROR_DIE("listen");
    }

    // 执行循环处理
    loop();
}

void Httpd::loop()
{
    pthread_t pthread;

    while (true)
    {
        struct sockaddr_in client_name;
        socklen_t client_name_len = sizeof(client_name);

        int fd = accept(m_socket_fd_,
                        (struct sockaddr *)&client_name,
                        &client_name_len);
        if (fd == -1)
        {
            ERROR_DIE("accept");
        }

        HttpdSocketPtr s = newObject();
        s->setClientFd(fd);
        s->setClientName(client_name);
        s->setHttpd(this);

        if (pthread_create(&pthread , NULL, accept_request, s) != 0)
        {
            ERROR("pthread_create");
        }
    }

    ::close(m_socket_fd_);
}

void HttpdSocket::serveFile(const char *path)
{
    FILE *resource = NULL;
    discardBody();

    resource = ::fopen(path, "r");
    LOG("path:%s", path);
    if (resource == NULL)
    {
        error404();
    }
    else
    {
        string s = string("HTTP/1.0 200 OK\r\n") + 
            SERVER_STRING + 
            "Content-Type: text/html\r\n" +
            "\r\n";
        ::send(m_client_fd_, s.c_str(), strlen(s.c_str()), 0);

        while (!::feof(resource))
        {
            m_buffer_[0] = '\0';
            ::fgets(m_buffer_, sizeof(m_buffer_), resource);
            LOG("buffer:%s", m_buffer_);
            ::send(m_client_fd_, m_buffer_, strlen(m_buffer_), 0);
        }
    }

    ::fclose(resource);
}

void HttpdSocket::executeCGI(const char* path)
{
    // 管道
    int cgi_output[2], cgi_input[2];
    pid_t pid;
    int status;
    char c;

    string s = string("HTTP/1.0 200 OK\r\n") + 
            SERVER_STRING + 
            "Content-Type: text/html\r\n" +
            "\r\n";
    ::send(m_client_fd_, s.c_str(), strlen(s.c_str()), 0);

    if (pipe(cgi_output) < 0) 
    {
        error500();
        return ;
    }

    if (pipe(cgi_input) < 0) 
    {
        error500();
        return ;
    }

    if ((pid = fork()) < 0) 
    {
        error500();
        return ;
    }

    int content_len = getContentLength();
    // 运行cgi脚本
    if (pid == 0)
    {
        char meth_env[255], query_env[255], length_env[255];

        ::dup2(cgi_output[1], 1);
        ::dup2(cgi_input[0], 0);

        ::close(cgi_output[0]);
        ::close(cgi_input[1]);

        snprintf(meth_env, sizeof(meth_env) - 1, "REQUEST_METHOD=%s", m_method_);
        putenv(meth_env);
        if (isGET()) 
        {
            sprintf(query_env, "QUERY_STRING=%s", m_query_);
            putenv(query_env);
        }
        else 
        {
            sprintf(length_env, "CONTENT_LENGTH=%d", content_len);
            putenv(length_env);
        }
        execl(path, path, NULL);
        exit(0);
    } 
    else
    {
        ::close(cgi_output[1]);
        ::close(cgi_input[0]);
        
        if (isPOST())
        {
            for (int i = 0; i < content_len; i++) 
            {
                ::recv(m_client_fd_, &c, 1, 0);
                ::write(cgi_input[1], &c, 1);
            }
        }
        LOG("pid:%d, content_len:%d", pid, content_len);
        while (::read(cgi_output[0], &c, 1) > 0)
        {
            ::send(m_client_fd_, &c, 1, 0);
        }
        ::close(cgi_output[0]);
        ::close(cgi_input[1]);

        ::waitpid(pid, &status, 0);
        LOG("status:%d", status);
    }
}

// 解析方法
void HttpdSocket::parseMethod()
{
    // 获取数据
    if (m_buffer_xi_ > m_buffer_len_ || m_buffer_len_ == 0)
    {
        getLine();
        m_buffer_xi_ = 0;

        LOG("body:%s", m_buffer_);
    }
    
    // 跳过空格
    while (isspace(m_buffer_[m_buffer_xi_]) && (m_buffer_xi_ < sizeof(m_buffer_))) m_buffer_xi_++;

    size_t xi = 0;
    while (!isspace(m_buffer_[m_buffer_xi_]) && 
        (xi < sizeof(m_method_) - 1) && 
        (m_buffer_xi_ < sizeof(m_buffer_)))
    {
        m_method_[xi] = m_buffer_[m_buffer_xi_];
        xi++; 
        m_buffer_xi_++;
    }

    m_method_[m_buffer_xi_] = '\0';

    LOG("method:%s, m_buffer_xi_:%d, m_buffer_len_:%d", 
        m_method_, m_buffer_xi_, m_buffer_len_);
}

void HttpdSocket::parseUrl()
{
    // 获取数据
    if (m_buffer_xi_ >= m_buffer_len_ || m_buffer_len_ == 0)
    {
        getLine();
        m_buffer_xi_ = 0;

        LOG("body:%s", m_buffer_);
    }

    while (isspace(m_buffer_[m_buffer_xi_]) && (m_buffer_xi_ < sizeof(m_buffer_))) m_buffer_xi_++;

    size_t xi = 0;
    while (!isspace(m_buffer_[m_buffer_xi_]) && 
        (xi < sizeof(m_url_) - 1) && 
        (m_buffer_xi_ < sizeof(m_buffer_)))
    {
        m_url_[xi] = m_buffer_[m_buffer_xi_];
        xi++; 
        m_buffer_xi_++;
    }

    m_url_[xi] = '\0';

    if (strcasecmp(m_method_, "GET") == 0)
    {
        m_query_ = m_url_;
        while ((*m_query_ != '?') && (*m_query_ != '\0')) m_query_++;
        if (*m_query_ == '?')
        {
            *m_query_ = '\0';
            m_query_++;
        }
    }

    LOG("m_url_:%s, m_buffer_xi_:%d, m_buffer_len_:%d", 
        m_url_, m_buffer_xi_, m_buffer_len_);
    // 跳过解析协议
    m_buffer_xi_ = m_buffer_len_;
}

void HttpdSocket::parseHeader()
{
    while (true)
    {
        // 获取数据
        if (m_buffer_xi_ >= m_buffer_len_ || m_buffer_len_ == 0)
        {
            getLine();
            m_buffer_xi_ = 0;

            LOG("body:%s, m_buffer_len_:%d", m_buffer_, m_buffer_len_);
        }

        while (isspace(m_buffer_[m_buffer_xi_]) && (m_buffer_xi_ < sizeof(m_buffer_))) m_buffer_xi_++;

        if (m_buffer_len_ <= 0)
        {
            break;
        }

        if (strcmp("\r", m_buffer_) == 0 ||
            strcmp("\n", m_buffer_) == 0)
        {
            // header的解析终止
            break;
        }

        LOG("header:%s", m_buffer_);
        string s1, s2;
        m_buffer_xi_ = split(m_buffer_, s1, s2);
        if (m_buffer_xi_ > 1)
        {
            m_header_[s1] = s2;
        }

        LOG("m_buffer_xi_:%d, m_buffer_len_:%d", m_buffer_xi_, m_buffer_len_);
    }

    LOG("parseHeader end");
}

void HttpdSocket::error501()
{
    string s = string("HTTP/1.0 501 Method Not Implemented\r\n") + 
        SERVER_STRING + 
        "Content-Type: text/html\r\n" +
        "\r\n" + 
        "<HTML><HEAD><TITLE>Method Not Implemented\r\n" +
        "</TITLE></HEAD>\r\n" +
        "<BODY><P>HTTP request method not supported.\r\n" +
        "</BODY></HTML>\r\n";

    ::send(m_client_fd_, s.c_str(), strlen(s.c_str()), 0);
}

void HttpdSocket::error500()
{
    string s = string("HTTP/1.0 500 Internal Server Error\r\n") + 
        "Content-Type: text/html\r\n" +
        "\r\n" + 
        "<P>Error prohibited CGI execution.\r\n";

    ::send(m_client_fd_, s.c_str(), strlen(s.c_str()), 0);
}

void HttpdSocket::error400()
{
    string s = string("HTTP/1.0 400 BAD REQUEST\r\n") + 
        "Content-type: text/html\r\n" +
        "\r\n" + 
        "<P>Your browser sent a bad request, " +
        "such as a POST without a Content-Length.\r\n";

    ::send(m_client_fd_, s.c_str(), strlen(s.c_str()), 0);
}

void HttpdSocket::error404()
{
    string s = string("HTTP/1.0 404 NOT FOUND\r\n") + 
        SERVER_STRING + 
        "Content-type: text/html\r\n" +
        "\r\n" + 
        "<HTML><TITLE>Not Found</TITLE>\r\n" +
        "<BODY><P>The server could not fulfill\r\n" +
        "your request because the resource specified\r\n" +
        "is unavailable or nonexistent.\r\n" +
        "</BODY></HTML>\r\n";

    ::send(m_client_fd_, s.c_str(), strlen(s.c_str()), 0);
}