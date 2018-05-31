#ifndef __FF_TCPIP_H__
#define __FF_TCPIP_H__

#define __LITTLE_ENDIAN_BITFIELD

typedef  int int32;
typedef  unsigned int u_int32;
typedef  unsigned char u_char;
typedef  unsigned short u_short;

#pragma pack(1)

typedef struct ip_hdr{//ipv4头部
#ifdef __LITTLE_ENDIAN_BITFIELD
	u_char ip_length:4,  
	ip_version:4;                
#else
	u_char ip_version:4,
	ip_length:4;
#endif
	u_char ip_tos;                        
	u_short ip_total_length;              
	u_short ip_id;                        
	u_short ip_flags;                    
	u_char ip_ttl;                        
	u_char ip_protocol;                  
	u_short ip_cksum;                    
	u_int32 ip_source;                    
	u_int32 ip_dest;                      
} IP_HDR;

typedef struct udp_hdr{//udp头部
	u_short s_port;      
	u_short d_port;      
	u_short length;      
	u_short cksum;      
} UDP_HDR;

typedef struct psd_header{//伪头部，用于计算校验和
	u_int32 s_ip;//source ip
	u_int32 d_ip;//dest ip
	u_char mbz;//0
	u_char proto;//proto type
	u_short plen;//length
} PSD_HEADER;

typedef struct mac_frame_hdr{
	u_char m_cDstMacAddress[6];   //目的mac地址
	u_char m_cSrcMacAddress[6];   //源mac地址
	u_short m_cType;              //上一层协议类型，如0x0800代表上一层是IP协议，0x0806为arp
} MAC_FRAME_HDR;

typedef struct linux_cooked_capture_hdr{
	u_short pcaket_type;   
	u_short link_layer_address_type;
	u_short link_layer_address_lenght;
	u_char source[6];
	u_short padding;
	u_short protocol;              //上一层协议类型，如0x0800代表上一层是IP协议，0x0806为arp
} LINUX_COOKED_CAPTURE_HDR;

typedef struct rtp_hdr{
#ifdef __LITTLE_ENDIAN_BITFIELD   //注意，各个机器大端小端是不同的，所以要根据实际情况修改此宏定义
	u_char csrc_count:4,
	extension:1,
	padding:1,
	version:2;
	u_char payload_type:7,
	marker:1;
#else
	u_char version:2, //RTP协议的版本号，占2位，当前协议版本号为2
	padding:1, //填充标志，占1位，如果P=1，则在该报文的尾部填充一个或多个额外的八位组，它们不是有效载荷的一部分
	extension:1, //扩展标志，占1位，如果X=1，则在RTP报头后跟有一个扩展报头。
	csrc_count:4; //CSRC计数器，占4位，指示CSRC 标识符的个数
	u_char  marker:1, //不同的有效载荷有不同的含义，对于视频，标记一帧的结束；对于音频，标记会话的开始
	payload_type:7; //有效载荷类型，占7位，用于说明RTP报文中有效载荷的类型，如GSM音频、JPEM图像等
#endif
	u_short seq; //序列号：占16位，用于标识发送者所发送的RTP报文的序列号，每发送一个报文，序列号增1
	u_int32 timestamp; //时戳：占32位，时戳反映了该RTP报文的第一个八位组的采样时刻。使用时戳来计算延迟和延迟抖动
	u_int32 ssrc; //占32位，用于标识同步信源。该标识符是随机选择的，参加同一视频会议的两个同步信源不能有相同的SSRC
	u_int32 csrc[0];
} RTP_HDR;

typedef struct rtp_extension_hdr{
	u_short define_by_profile;
	u_short extension_length;
	u_int32 header_extension[0];
} RTP_EXTENSION_HDR;

typedef struct pcap_file_header {
	u_int32 magic;
	u_short version_major;
	u_short version_minor;
	int32   thiszone;    
	u_int32 sigfigs;    
	u_int32 snaplen;    
	u_int32 linktype;  
} PCAP_FILE_HDR;


typedef struct pcap_pkt_hdr {
	u_int32 iTimeSecond;
	u_int32 iTimeSS;
	u_int32 caplen;    
	u_int32 len;        
} PCAP_PKT_HDR;

#define MAX_AUDIO_BUFFERS_SIZE 30
#define MAX_PCM_BUFFERS_SIZE 90


/**
 * 自定义的audio缓冲队列，实现了jitterbuffer功能，消除RTP乱序、丢包
 */
typedef struct audio_buffers{
	char* data[MAX_AUDIO_BUFFERS_SIZE];
	u_short seq[MAX_AUDIO_BUFFERS_SIZE];
	int size;
	int buffer_size;
	int hdr_size;
	char* hdr;
} ADUIO_BUFFERS;

/**
 * PCM缓冲队列，多线程（解码线程、播放线程）共同使用时需上锁mutex
 */
typedef struct pcm_buffers{
	u_char* data[MAX_PCM_BUFFERS_SIZE];
	int size;
	int buffer_size;
} PCM_BUFFERS;

typedef struct audio_info{
	u_int32 ip_src;
	u_int32 ip_dst;
	u_int32 ssrc;
	int type_num;
	char a_line[5];
	int pkt_count;
	int max_kbps;
	int fec;
	int ptime;
} AUDIO_INFO;

#pragma pack()

#endif
