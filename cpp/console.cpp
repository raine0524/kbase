#include "stdafx.h"

std::string g_server_name;		//当前服务的名称

crx::logger g_lib_log, g_app_log;

namespace crx
{
    int simpack_protocol(char *data, size_t len)
    {
        if (len < sizeof(simp_header))      //还未取到simp协议头
            return 0;

        uint32_t magic_num = ntohl(*(uint32_t *)data);
        if (0x5f3759df != magic_num) {
            size_t i = 1;
            for (; i < len; ++i) {
                magic_num = ntohl(*(uint32_t*)(data+i));
                if (0x5f3759df == magic_num)
                    break;
            }

            if (i < len)        //在后续流中找到该魔数，截断之前的无效流
                return -(int)i;
            else        //未找到，截断整个无效流
                return -(int)len;
        }

        auto header = (simp_header*)data;
        int ctx_len = ntohl(header->length)+sizeof(simp_header);

        if (len < ctx_len)
            return 0;
        else
            return ctx_len;
    }

    console_impl::console_impl(console *c)
    :m_c(c)
    ,m_is_service(false)
    ,m_as_shell(false)
    ,m_close_exp(true)
    ,m_conn(-1)
    {
        simp_header stub_header;
        m_simp_buf = std::string((const char*)&stub_header, sizeof(simp_header));

        m_cmd_vec.push_back({"h", std::bind(&console_impl::print_help, this, _1), "显示帮助"});
        m_cmd_vec.push_back({"q", std::bind(&console_impl::quit_loop, this, _1), "退出程序"});
    }

    bool console_impl::check_service_exist()
    {
        net_socket sock(UNIX_DOMAIN);
        int ret = sock.create();
        close(sock.m_sock_fd);
        return ret < 0;
    }

    //该函数用于连接后台daemon进程
    void console_impl::connect_service()
    {
        m_client = m_c->get_tcp_client(std::bind(&console_impl::tcp_callback, this, true, _1, _2, _3, _4, _5));
        m_c->register_tcp_hook(true, [](int conn, char *data, size_t len) { return simpack_protocol(data, len); });
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        sch_impl->m_util_impls[TCP_CLI].reset();

        auto cli_impl = std::dynamic_pointer_cast<tcp_client_impl>(m_client.m_impl);
        cli_impl->m_util.m_app_prt = PRT_SIMP;
        cli_impl->m_util.m_type = UNIX_DOMAIN;
        m_conn = m_client.connect("127.0.0.1", 0);
    }

    void console_impl::stop_service(bool pout)
    {
        m_close_exp = false;
        m_simp_buf.resize(sizeof(simp_header));
        ((simp_header*)&m_simp_buf[0])->length = htonl(UINT32_MAX);
        m_client.send_data(m_conn, m_simp_buf.data(), m_simp_buf.length());

        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        sch_impl->main_coroutine();      //进入主协程
        m_client.release(m_conn);
        m_client.m_impl.reset();
        m_conn = -1;
        if (pout)
            printf("后台服务 %s 正常关闭，退出当前shell\n", g_server_name.c_str());
    }

    void console_impl::tcp_callback(bool client, int conn, const std::string& ip_addr, uint16_t port, char *data, size_t len)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        if (client) {
            if (data) {
                std::cout<<data+sizeof(simp_header)<<std::flush;
            } else if (!len) {      //对端连接已关闭
                sch_impl->m_go_done = false;        //退出当前shell进程
                if (m_close_exp)
                    printf("\n后台服务 %s 异常关闭\n", g_server_name.c_str());
            }
            return;
        }

        //server
        if (!data && !len) {        //shell进程已关闭,释放此处连接资源
            m_conn = -1;
            return;
        }

        std::vector<std::string> args;
        uint32_t length = ntohl(((simp_header*)data)->length);
        if (UINT32_MAX == length) {     //停止daemon进程
            quit_loop(args);
            return;
        }

        auto str_vec = split(data+sizeof(simp_header), length, " ");
        std::string cmd;
        for (size_t i = 0; i < str_vec.size(); ++i) {
            auto& arg = str_vec[i];
            if (0 == i)
                cmd = std::string(arg.data, arg.len);
            else
                args.emplace_back(arg.data, arg.len);
        }
        execute_cmd(cmd, args);
    }

    //控制台预处理操作
    bool console_impl::preprocess(int argc, char *argv[])
    {
        std::vector<std::string> stub_args;
        if (argc >= 2) {		//当前服务以带参模式运行，检测最后一个参数是否为{"-start", "-stop", "-restart"}之一
            bool exist = check_service_exist();
            if (!strcmp("-start", argv[argc-1])) {      //启动服务
                if (exist) {    //检测后台服务是否处于运行过程中
                    printf("当前服务 %s 正在后台运行中\n", g_server_name.c_str());
                    return true;
                }
                start_daemon();
            }

            if (!strcmp("-stop", argv[argc-1])) {       //终止服务
                if (exist) {            //确认当前服务正在运行中，执行终止操作
                    connect_service();
                    stop_service(true);
                } else {
                    printf("服务 %s 不在后台运行\n", g_server_name.c_str());
                }
                return true;		//通知后台进程退出之后也没必要继续运行
            }

            if (!strcmp("-restart", argv[argc-1])) {    //重启服务
                if (exist) {
                    connect_service();
                    stop_service(false);
                }
                start_daemon();
            }
        }

        //当前进程不是daemon进程并且后台服务存在，此时连接后台服务并以shell方式运行
        if (!m_is_service && check_service_exist()) {
            m_as_shell = true;
            connect_service();      //连接后台服务
            listen_keyboard_input();

            auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
            sch_impl->main_coroutine();
            return true;
        }
        return false;		//预处理失败表示在当前环境下还需要做进一步处理
    }

    void console_impl::execute_cmd(const std::string& cmd, std::vector<std::string>& args)
    {
        for (auto& con : m_cmd_vec)
            if (cmd == con.cmd)
                con.f(args);
    }

    /*
     * 函数用于等待shell输入
     * @当前程序以普通进程运行时将调用该例程等待命令行输入
     * @当前程序以shell方式运行时将调用该例程将命令行输入参数传给后台运行的服务
     */
    void console_impl::listen_keyboard_input()
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        std::cout<<g_server_name<<":\\>"<<std::flush;

        auto ev = std::make_shared<eth_event>();
        ev->fd = STDIN_FILENO;
        setnonblocking(STDIN_FILENO);
        ev->sch_impl = sch_impl;
        ev->f = [this](uint32_t events) {
            std::string input;
            int ret = async_read(STDIN_FILENO, input);
            if (ret <= 0) {
                perror("read keyboard failed");
                return;
            }

            input.pop_back();   //读终端输入时去掉最后一个换行符'\n'
            trim(input);        //移除输入字符串前后的空格符
            if (input.empty()) {
                std::cout<<g_server_name<<":\\>"<<std::flush;
                return;
            }

            //命令行输入的一系列参数都是以空格分隔
            auto str_vec = split(input.data(), input.size(), " ");
            std::string cmd;
            std::vector<std::string> args;
            for (size_t i = 0; i < str_vec.size(); ++i) {
                auto& arg = str_vec[i];
                if (0 == i)
                    cmd = std::string(arg.data, arg.len);
                else
                    args.emplace_back(arg.data, arg.len);
            }

            bool find_cmd = false;
            for (auto& con : m_cmd_vec) {
                if (cmd == con.cmd) {
                    find_cmd = true;
                    break;
                }
            }
            if (!find_cmd) {        //若需要执行的命令并未通过add_cmd接口添加时，通知该命令未知
                std::cout<<"未知命令或参数！「请输入help(h)显示帮助」\n\n" <<g_server_name<<":\\>"<<std::flush;
                return;
            }

            if (m_as_shell) {       //当前程序以shell方式运行
                if ("h" == input || "q" == input) {     //输入为"h"(帮助)或者"q"(退出)时直接执行相应命令
                    execute_cmd(cmd, args);
                    if ("h" == input)
                        std::cout<<g_server_name<<":\\>"<<std::flush;
                } else {        //否则将命令行参数传给后台daemon进程
                    m_simp_buf.resize(sizeof(simp_header));
                    m_simp_buf.append(input);
                    ((simp_header*)&m_simp_buf[0])->length = htonl((uint32_t)input.length());
                    m_client.send_data(m_conn, m_simp_buf.data(), m_simp_buf.length());
                }
            } else {
                execute_cmd(cmd, args);
            }
        };
        sch_impl->add_event(ev);
    }

    void console_impl::start_daemon()
    {
        //创建一个unix域server
        m_server = m_c->get_tcp_server(-1, std::bind(&console_impl::tcp_callback, this, false, _1, _2, _3, _4, _5));
        auto svr_impl = std::dynamic_pointer_cast<tcp_server_impl>(m_server.m_impl);
        svr_impl->m_util.m_app_prt = PRT_SIMP;
        m_c->register_tcp_hook(false, [](int conn, char *data, size_t len) { return simpack_protocol(data, len); });
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        sch_impl->m_util_impls[TCP_SVR].reset();

        m_is_service = true;
        daemon(1, 0);       //创建守护进程，不切换进程当前工作目录
    }

    void console_impl::quit_loop(std::vector<std::string>& args)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        for (auto& co_impl : sch_impl->m_cos)
            co_impl->status = CO_UNKNOWN;

        if (!m_as_shell)        //以shell方式运行时不需要执行init/destroy操作
            m_c->destroy();

        for (auto& new_sch : m_schs) {
            if (new_sch->m_th.joinable()) {
                new_sch->m_go_done = false;
                new_sch->m_th.join();
            }
        }
        m_schs.clear();

        auto etcd_impl = std::dynamic_pointer_cast<etcd_client_impl>(sch_impl->m_util_impls[ETCD_CLI]);
        if (!etcd_impl->m_worker_path.empty())
            etcd_impl->m_get_wsts = 3;      // bye to etcd
        else
            sch_impl->m_go_done = false;
    }

    //打印帮助信息
    void console_impl::print_help(std::vector<std::string>& args)
    {
        std::cout<<std::right<<std::setw(5)<<"cmd";
        std::cout<<std::setw(10)<<' '<<std::setw(0)<<"comments\n";
        for (auto& cmd : m_cmd_vec) {
            std::string str = "  "+cmd.cmd;
            std::cout<<std::left<<std::setw(15)<<str<<cmd.comment<<'\n';
        }
        std::cout<<std::endl;
    }

    console::console()
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        sch_impl->m_util_impls[EXT_DATA] = std::make_shared<console_impl>(this);
    }

    //添加控制台命令
    void console::add_cmd(const char *cmd, std::function<void(std::vector<std::string>&)> f,
                          const char *comment)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto con_impl = std::dynamic_pointer_cast<console_impl>(sch_impl->m_util_impls[EXT_DATA]);
        for (auto& con : con_impl->m_cmd_vec)
            assert(cmd != con.cmd);
        con_impl->m_cmd_vec.push_back({cmd, f, comment});
    }

    void console::pout(const char *fmt, ...)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto con_impl = std::dynamic_pointer_cast<console_impl>(sch_impl->m_util_impls[EXT_DATA]);
        if (!con_impl->m_is_service || con_impl->m_conn < 0) {
            va_list val;
            va_start(val, fmt);
            vprintf(fmt, val);
            va_end(val);
            return;
        }

        va_list vl1, vl2;
        va_start(vl1, fmt);
        va_copy(vl2, vl1);
        int ret = vsnprintf(nullptr, 0, fmt, vl1);
        std::string buf(ret+1, 0);
        vsnprintf(&buf[0], buf.size(), fmt, vl2);
        va_end(vl1);
        va_end(vl2);

        con_impl->m_simp_buf.resize(sizeof(simp_header));
        con_impl->m_simp_buf.append(buf);
        ((simp_header*)&con_impl->m_simp_buf[0])->length = htonl((uint32_t)ret);
        con_impl->m_server.send_data(con_impl->m_conn, con_impl->m_simp_buf.data(), con_impl->m_simp_buf.size());
    }

    scheduler console::clone()
    {
        scheduler sch;
        auto new_sch = std::make_shared<scheduler_impl>();
        new_sch->m_epoll_fd = epoll_create(EPOLL_SIZE);
        if (__glibc_unlikely(-1 == new_sch->m_epoll_fd)) {
            g_lib_log.printf(LVL_ERROR, "epoll create failed: %s\n", strerror(errno));
            return sch;
        }

        std::function<void(size_t co_id)> stub;
        new_sch->co_create(stub, true, false, "main coroutine");        //创建主协程

        if (!new_sch->m_wheel.m_impl)
            new_sch->m_wheel = get_timer_wheel();

        new_sch->m_th = std::thread(std::bind(&scheduler_impl::main_coroutine, new_sch.get()));
        sch.m_impl = new_sch;

        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto con_impl = std::dynamic_pointer_cast<console_impl>(sch_impl->m_util_impls[EXT_DATA]);
        con_impl->m_schs.push_back(new_sch);
        return sch;
    }

    void console_impl::bind_core(int which)
    {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(which, &mask);
        if (syscall(__NR_gettid) == getpid()) {     //main thread
            if (__glibc_unlikely(sched_setaffinity(0, sizeof(mask), &mask) < 0))
                g_lib_log.printf(LVL_ERROR, "bind core failed: %s\n", strerror(errno));
        } else {
            if (__glibc_unlikely(pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) < 0))
                g_lib_log.printf(LVL_ERROR, "bind core failed: %s\n", strerror(errno));
        }
    }

    void console_impl::parse_config(const char *config)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
        ini ini_parser;
        ini_parser.load(config);
        if (ini_parser.has_section("logger")) {     // 日志
            ini_parser.set_section("logger");
            sch_impl->m_log_lvl = (LOG_LEVEL)ini_parser.get_int("level");
            sch_impl->m_log_root = ini_parser.get_str("root_dir");
            sch_impl->m_back_cnt = ini_parser.get_int("backup_cnt");

            mkdir_multi(sch_impl->m_log_root.c_str());
            g_lib_log.m_impl = sch_impl->get_logger(m_c, "kbase", 1);
            g_app_log.m_impl = sch_impl->get_logger(m_c, g_server_name.c_str(), 2);
        }

        auto etcd_impl = std::make_shared<etcd_client_impl>();
        if (ini_parser.has_section("etcd")) {       // etcd
            ini_parser.set_section("etcd");
            etcd_impl->m_sch_impl = sch_impl;
            etcd_impl->m_http_client = m_c->get_http_client(std::bind(&etcd_client_impl::http_client_callback,
                    etcd_impl.get(), _1, _2, _3, _4, _5));

            auto http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_client_impl>>(etcd_impl->m_http_client.m_impl);
            http_impl->m_util.m_f = [this](int fd, const std::string& ip_addr, uint16_t port, char *data, size_t len) {
                auto this_sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_c->m_impl);
                auto this_etcd_impl = std::dynamic_pointer_cast<etcd_client_impl>(this_sch_impl->m_util_impls[ETCD_CLI]);
                auto this_http_impl = std::dynamic_pointer_cast<http_impl_t<tcp_client_impl>>(this_etcd_impl->m_http_client.m_impl);
                tcp_callback_for_http<std::shared_ptr<http_impl_t<tcp_client_impl>>, tcp_client_conn>(
                        true, this_http_impl, fd, data, len);
            };

            sch_impl->m_util_impls[HTTP_CLI].reset();

            if (ini_parser.has_key("endpoints")) {
                auto endpoints = ini_parser.get_str("endpoints");
                auto ep_vec = split(endpoints.c_str(), endpoints.size(), ";");
                for (auto& ep : ep_vec) {
                    auto ip_port = split(ep.data, ep.len, ":");
                    if (2 != ip_port.size())
                        g_lib_log.printf(LVL_FATAL, "illegal endpoints: %v\n", endpoints.c_str());

                    endpoint point;
                    strncpy(point.ip_addr, ip_port[0].data, ip_port[0].len);
                    point.port = atoi(ip_port[1].data);
                    etcd_impl->m_endpoints.push_back(point);
                }
            }

            std::string service_name, node_name;
            if (ini_parser.has_key("node_name"))
                node_name = ini_parser.get_str("node_name");

            if (ini_parser.has_key("listen_addr")) {
                std::string addr = ini_parser.get_str("listen_addr");
                auto ip_port = split(addr.c_str(), addr.size(), ":");
                if (2 != ip_port.size())
                    g_lib_log.printf(LVL_FATAL, "illegal ip & port format: %v\n", addr.c_str());

                strncpy(etcd_impl->m_worker_addr.ip_addr, ip_port[0].data, ip_port[0].len);
                etcd_impl->m_worker_addr.port = atoi(ip_port[1].data);
            }

            if (ini_parser.has_key("worker_path")) {
                etcd_impl->m_worker_path = ini_parser.get_str("worker_path");
                if ('/' == etcd_impl->m_worker_path.back())
                    etcd_impl->m_worker_path.pop_back();

                auto pos = etcd_impl->m_worker_path.rfind('/');
                if (std::string::npos == pos)
                    g_lib_log.printf(LVL_FATAL, "illegal etcd worker path: %s\n", etcd_impl->m_worker_path.c_str());

                service_name = etcd_impl->m_worker_path.substr(pos+1);
                etcd_impl->m_worker_path = std::string("/v2/keys")+etcd_impl->m_worker_path+"/"+node_name;
            }

            if (!node_name.empty() && etcd_impl->m_worker_addr.ip_addr[0]
                    && !etcd_impl->m_worker_path.empty()) {
                rapidjson::Document doc;
                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

                doc.SetObject();
                auto& alloc = doc.GetAllocator();
                doc.AddMember("service", rapidjson::Value().SetString(service_name.c_str(), (int)service_name.size(), alloc), alloc);
                doc.AddMember("node", rapidjson::Value().SetString(node_name.c_str(), (int)node_name.size(), alloc), alloc);
                doc.AddMember("ip", rapidjson::Value().SetString(etcd_impl->m_worker_addr.ip_addr,
                        (int)strlen(etcd_impl->m_worker_addr.ip_addr)), alloc);
                doc.AddMember("port", etcd_impl->m_worker_addr.port, alloc);

                doc.Accept(writer);
                etcd_impl->m_worker_value = std::string("value=")+buffer.GetString()+"&&ttl=5";
                etcd_impl->periodic_heart_beat();
            }

            if (ini_parser.has_key("watch_paths")) {
                auto paths = ini_parser.get_str("watch_paths");
                auto path_vec = split(paths.c_str(), paths.size(), ";");
                for (int i = 0; i < path_vec.size(); i++) {
                    auto& path = path_vec[i];
                    if ('/' == path.data[path.len-1])
                        path.len -= 1;

                    etcd_master_info info;
                    info.valid = false;
                    info.nonwait_path = std::string("/v2/keys")+std::string(path.data, path.len);
                    info.wait_path = info.nonwait_path+"?wait=true&recursive=true";
                    info.conn = -1;

                    auto pos = info.nonwait_path.rfind('/');
                    if (std::string::npos == pos)
                        g_lib_log.printf(LVL_FATAL, "illegal etcd watch path: %s\n", info.nonwait_path.c_str());

                    info.service_name = info.nonwait_path.substr(pos+1);
                    info.rr_idx = 0;
                    etcd_impl->m_master_infos.emplace_back(info);
                    etcd_impl->periodic_wait_event(i);
                }
            }
        }
        sch_impl->m_util_impls[ETCD_CLI] = etcd_impl;
    }

    int console::run(int argc, char *argv[], const char *conf /*= "ini/server.ini"*/, int bind_flag /*= -1*/)
    {
        //get server name
        g_server_name = argv[0];
        g_server_name = g_server_name.substr(g_server_name.rfind("/")+1);

        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto con_impl = std::dynamic_pointer_cast<console_impl>(sch_impl->m_util_impls[EXT_DATA]);

        srand((unsigned int)time(nullptr));
        sch_impl->m_epoll_fd = epoll_create(EPOLL_SIZE);
        if (__glibc_unlikely(-1 == sch_impl->m_epoll_fd)) {
            printf("epoll_create failed: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }

        std::function<void(size_t co_id)> stub;
        sch_impl->co_create(stub, true, false, "main coroutine");        //创建主协程

        /*
         * 首先执行预处理操作，预处理主要和当前运行环境以及运行时所带参数有关，在预处理操作中可能只是简单的停止后台服务，或连接后台服务
         * 执行一些命令，此时只需要有一个主协程，并进入主协程进行简单的读写操作即可，在这种使用场景下当前进程仅仅只是真实服务的一个shell，
         * 不需要使用日志或者创建其他运行时需要用到的资源
         */
        if (con_impl->preprocess(argc, argv))
            return EXIT_SUCCESS;

        //处理绑核操作
        int cpu_num = get_nprocs();
        if (-1 != bind_flag) {
            if (INT_MAX == bind_flag)
                con_impl->bind_core(rand()%cpu_num);
            else if (0 <= bind_flag && bind_flag < cpu_num)     //bind_flag的取值范围为0~cpu_num-1
                con_impl->bind_core(bind_flag);
        }

        signal(SIGPIPE, SIG_IGN);       //向已经断开连接的管道和套接字写数据时只返回错误,不主动退出进程
        if (!sch_impl->m_wheel.m_impl)       //创建一个定时轮
            sch_impl->m_wheel = get_timer_wheel();
//        sch_impl->m_wheel.add_handler(5*1000, std::bind(&scheduler_impl::periodic_trim_memory, sch_impl.get()));

        if (!access(conf, F_OK))        //解析日志配置
            con_impl->parse_config(conf);

        //打印帮助信息
        std::vector<std::string> str_vec;
        con_impl->print_help(str_vec);

        if (!init(argc, argv))		//执行初始化，若初始化返回失败，直接退出当前进程
            return EXIT_FAILURE;

        if (!con_impl->m_is_service)        //前台运行时在控制台输入接收命令
            con_impl->listen_keyboard_input();

        sch_impl->main_coroutine();     //进入主协程
        return EXIT_SUCCESS;
    }
}
