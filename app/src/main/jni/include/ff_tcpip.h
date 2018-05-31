#ifndef __FF_TCPIP_H__
#define __FF_TCPIP_H__

#define __LITTLE_ENDIAN_BITFIELD

typedef  int int32;
typedef  unsigned int u_int32;
typedef  unsigned char u_char;
typedef  unsigned short u_short;

#pragma pack(1)

typedef struct ip_hdr{//ipv4ͷ��
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

typedef struct udp_hdr{//udpͷ��
	u_short s_port;      
	u_short d_port;      
	u_short length;      
	u_short cksum;      
} UDP_HDR;

typedef struct psd_header{//αͷ�������ڼ���У���
	u_int32 s_ip;//source ip
	u_int32 d_ip;//dest ip
	u_char mbz;//0
	u_char proto;//proto type
	u_short plen;//length
} PSD_HEADER;

typedef struct mac_frame_hdr{
	u_char m_cDstMacAddress[6];   //Ŀ��mac��ַ
	u_char m_cSrcMacAddress[6];   //Դmac��ַ
	u_short m_cType;              //��һ��Э�����ͣ���0x0800������һ����IPЭ�飬0x0806Ϊarp
} MAC_FRAME_HDR;

typedef struct linux_cooked_capture_hdr{
	u_short pcaket_type;   
	u_short link_layer_address_type;
	u_short link_layer_address_lenght;
	u_char source[6];
	u_short padding;
	u_short protocol;              //��һ��Э�����ͣ���0x0800������һ����IPЭ�飬0x0806Ϊarp
} LINUX_COOKED_CAPTURE_HDR;

typedef struct rtp_hdr{
#ifdef __LITTLE_ENDIAN_BITFIELD   //ע�⣬�����������С���ǲ�ͬ�ģ�����Ҫ����ʵ������޸Ĵ˺궨��
	u_char csrc_count:4,
	extension:1,
	padding:1,
	version:2;
	u_char payload_type:7,
	marker:1;
#else
	u_char version:2, //RTPЭ��İ汾�ţ�ռ2λ����ǰЭ��汾��Ϊ2
	padding:1, //����־��ռ1λ�����P=1�����ڸñ��ĵ�β�����һ����������İ�λ�飬���ǲ�����Ч�غɵ�һ����
	extension:1, //��չ��־��ռ1λ�����X=1������RTP��ͷ�����һ����չ��ͷ��
	csrc_count:4; //CSRC��������ռ4λ��ָʾCSRC ��ʶ���ĸ���
	u_char  marker:1, //��ͬ����Ч�غ��в�ͬ�ĺ��壬������Ƶ�����һ֡�Ľ�����������Ƶ����ǻỰ�Ŀ�ʼ
	payload_type:7; //��Ч�غ����ͣ�ռ7λ������˵��RTP��������Ч�غɵ����ͣ���GSM��Ƶ��JPEMͼ���
#endif
	u_short seq; //���кţ�ռ16λ�����ڱ�ʶ�����������͵�RTP���ĵ����кţ�ÿ����һ�����ģ����к���1
	u_int32 timestamp; //ʱ����ռ32λ��ʱ����ӳ�˸�RTP���ĵĵ�һ����λ��Ĳ���ʱ�̡�ʹ��ʱ���������ӳٺ��ӳٶ���
	u_int32 ssrc; //ռ32λ�����ڱ�ʶͬ����Դ���ñ�ʶ�������ѡ��ģ��μ�ͬһ��Ƶ���������ͬ����Դ��������ͬ��SSRC
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
 * �Զ����audio������У�ʵ����jitterbuffer���ܣ�����RTP���򡢶���
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
 * PCM������У����̣߳������̡߳������̣߳���ͬʹ��ʱ������mutex
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
