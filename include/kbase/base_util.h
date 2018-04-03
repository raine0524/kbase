#pragma once

namespace crx
{
    //base64编码
    std::string CRX_SHARE base64_encode(const char *data, int len);

    //base64解码
    std::string CRX_SHARE base64_decode(const char *data, int len);

    //将fd指定的文件设置为非阻塞
    bool CRX_SHARE setnonblocking(int fd);

    //在执行(fork-)exec调用之后关闭该描述符
    bool CRX_SHARE setcloseonexec(int fd);

    //打印当前执行点的调用堆栈
    void CRX_SHARE dump_segment();

    //检查符号链接文件是否存在
    bool CRX_SHARE symlink_exist(const char *file);

    //一次创建多级目录，所有目录的都采用相同的mode
    void CRX_SHARE mkdir_multi(const char *path, mode_t mode = 0755);

    //字符集转换
    std::string CRX_SHARE charset_convert(const char *from_charset, const char *to_charset, const char *src_data, int src_len);

    //获取本机地址(mac/ip等)
    std::string CRX_SHARE get_local_addr(ADDR_TYPE type, const char *net_card = "eth0");

    //获取当前进程的工作路径
    std::string CRX_SHARE get_current_working_path();

    //获取当前日期时间
    datetime CRX_SHARE get_current_datetime();

    //计算指定日期为星期几 @date: 20170621 @return: 1表示星期一，依次类推，7表示星期日
    int CRX_SHARE get_week_day(unsigned int date);

    //计算指定日期N天前后的日期 @spec_day: 指定日期(20170718) @N: N天前后(N>0表示N天后，反之为N天前) @return: 返回N天前后的日期
    unsigned int CRX_SHARE get_Nth_day(unsigned int spec_day, int N);

    //获取指定文件的大小
    int CRX_SHARE get_file_size(const char *file);

    /*
     * 获取字符串src中第n个pattern出现的位置，若n值为-1则查找src串中的最后一个pattern
     * @src 待查找模式串的原始字符串
     * @pattern 要查找的模式串
     * @n 查找在原始字符串中出现的第n+1个模式串，若查找首次出现的模式串的位置，则n设为0
     * @return value: 若找到则返回符合要求的模式串在原始串中的位置(从0开始)，未找到则返回-1
     */
    int CRX_SHARE find_nth_pos(const std::string& src, const char *pattern, int n);

    //将字符串前后的空格及制表符去除
    std::string CRX_SHARE &trim(std::string& s);

    //将字符串s按照分隔符delimiters进行分隔
    std::vector<std::string> CRX_SHARE split(const char *src, size_t len, const char *delim);

    /*
     * 将域名形式的主机地址server转换为点分十进制格式的ip地址，成功则返回true，反之返回false。若server已经是ip地址，
     * 则该函数执行server = ip_addr并返回true(使用glibc库中的gethostbyname进行域名解析，该函数将阻塞当前执行流
     * 且是不可重入的，因此当前例程主要是用来进行测试用的。在tcp_client类中的connect接口采用异步的方式进行域名解析)
     */
    bool CRX_SHARE convert_ipaddr(const char *server, std::string& ip_addr);

    //对根目录进行深度优先遍历，对于所有取得的文件都执行f函数调用
    void  CRX_SHARE depth_first_traverse_dir(const char *root_dir,
                                             std::function<void(const std::string&, void*)>f, void *args,
                                             bool with_path = true);

    //执行shell命令，并返回shell输出
    std::string CRX_SHARE run_shell_cmd(const char *cmd_string);
}