#pragma once

#define CRX_EXPORT_SYMBOL

#include "../include/crx_pch.h"

//////////////////////////////////////////////////////////////////////////
//kbase library (self custome implementation)
//msgpack
#include "../include/msgpack.hpp"

#include "pywrap_impl.h"

#include "net_base.h"
#include "scheduler_impl.h"
#include "schutil_impl.h"
#include "tcp_proto_impl.h"
#include "http_proto_impl.h"
#include "simpack_impl.h"
#include "logger_impl.h"
#include "fs_monitor_impl.h"
#include "console_impl.h"

extern std::unordered_map<int, std::string> g_ext_type;

enum CTL_CMD
{
    CMD_REG_NAME = 0,       //名字注册
    CMD_SVR_ONLINE,         //服务上线
    CMD_HELLO,              //握手
    CMD_GOODBYE,            //挥手
    CMD_CONN_CON,           //连接建立
    CMD_CONN_DES,           //连接断开
    CMD_MAX_VAL,
};