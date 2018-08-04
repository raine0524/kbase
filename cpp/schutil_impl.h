#pragma once

#include "stdafx.h"

namespace crx
{
    class sigctl_impl : public eth_event
    {
    public:
        sigctl_impl()
        {
            sigemptyset(&m_mask);
            bzero(&m_fd_info, sizeof(m_fd_info));
        }

        void sigctl_callback();

        void handle_sig(int signo, bool add);

        sigset_t m_mask;
        signalfd_siginfo m_fd_info;
        std::map<int, std::function<void(uint64_t)>> m_sig_cb;
    };

    class timer_impl : public eth_event
    {
    public:
        timer_impl() : m_delay(0), m_interval(0) {}

        void timer_callback();

        uint64_t m_delay, m_interval;        //分别对应于首次触发时的延迟时间以及周期性触发时的间隔时间
        std::function<void()> m_f;
    };

    class timer_wheel_impl : public impl
    {
    public:
        timer_wheel_impl() : m_slot_idx(0) {}

        virtual ~timer_wheel_impl() { m_timer.detach(); }

        void timer_wheel_callback();

        timer m_timer;
        size_t m_slot_idx;
        std::vector<std::list<std::function<void()>>> m_slots;
    };

    class event_impl : public eth_event
    {
    public:
        void event_callback();

        std::list<int> m_signals;		//同一个事件可以由多个线程通过发送不同的信号同时触发
        std::function<void(int)> m_f;
    };

    class udp_ins_impl : public eth_event
    {
    public:
        udp_ins_impl() : m_recv_buffer(65536, 0)
        {
            bzero(&m_send_addr, sizeof(m_send_addr));
        }

        void udp_ins_callback();

        struct sockaddr_in m_send_addr, m_recv_addr;
        socklen_t m_recv_len;

        net_socket m_net_sock;			//udp套接字
        std::string m_recv_buffer;		//接收缓冲区
        std::function<void(const std::string&, uint16_t, char*, size_t)> m_f;       //收到数据时触发的回调函数
    };
}