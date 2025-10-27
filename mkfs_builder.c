// === Member 1 ===
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

// === Member 1 === Constants
#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define GROUP_ID 5u

// === Member 2 === CRC32 implementation
static uint32_t crc32_table[256];
static void crc32_init(void){
    uint32_t poly = 0xEDB88320u;
    for(uint32_t i=0;i<256;i++){
        uint32_t c=i;
        for(int j=0;j<8;j++) c=(c&1)?(poly^(c>>1)):(c>>1);
        crc32_table[i]=c;
    }
}
static uint32_t crc32_compute(const void* data, size_t n){
    const uint8_t* p=(const uint8_t*)data;
    uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c = crc32_table[(c^p[i])&0xFFu] ^ (c>>8);
    return c ^ 0xFFFFFFFFu;
}

// === Member 2 === On-disk structures
#pragma pack(push,1)
typedef struct {
    uint32_t magic, version, block_size;
    uint64_t total_blocks, inode_count;
    uint64_t inode_bitmap_start, inode_bitmap_blocks;
    uint64_t data_bitmap_start,  data_bitmap_blocks;
    uint64_t inode_table_start,  inode_table_blocks;
    uint64_t data_region_start,  data_region_blocks;
    uint64_t root_inode, mtime_epoch;
    uint32_t flags, checksum;
} superblock_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    uint16_t mode, links;
    uint32_t uid, gid;
    uint64_t size_bytes, atime, mtime, ctime;
    uint32_t direct[12];
    uint32_t reserved_0, reserved_1, reserved_2;
    uint32_t proj_id;
    uint32_t uid16_gid16;
    uint64_t xattr_ptr, inode_crc;
} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t)==INODE_SIZE, "inode size mismatch");

#pragma pack(push,1)
typedef struct {
    uint32_t inode_no;
    uint8_t  type;
    char     name[58];
    uint8_t  checksum;
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t)==64, "dirent64 size mismatch");

// === Member 2 === checksum helpers
static void superblock_crc_finalize(superblock_t* sb){
    sb->checksum=0;
    uint32_t c=crc32_compute(sb, sizeof(*sb)-sizeof(sb->checksum));
    sb->checksum=c;
}
static void inode_crc_finalize(inode_t* ino){
    ino->inode_crc=0;
    uint32_t c=crc32_compute(ino,120);
    ino->inode_crc=(uint64_t)c;
}
static void dirent_checksum_finalize(dirent64_t* de){
    const uint8_t* p=(const uint8_t*)de; uint8_t x=0;
    for(int i=0;i<63;i++) x^=p[i];
    de->checksum=x;
}

// === Member 2 === bitmap helper
static void bitmap_set(uint8_t* bm, uint64_t idx){
    bm[idx/8] |= (1u<<(idx%8));
}

// === Member 1 === CLI parser
typedef struct { const char* image; uint64_t size_kib,inodes; } cli_t;
static void usage(const char* prog){
    fprintf(stderr,"Usage: %s --image out.img --size-kib <180..4096> --inodes <128..512>\n", prog);
}
static int parse_cli(int argc,char** argv,cli_t* out){
    memset(out,0,sizeof(*out));
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--image")==0 && i+1<argc) out->image=argv[++i];
        else if(strcmp(argv[i],"--size-kib")==0 && i+1<argc) out->size_kib=strtoull(argv[++i],NULL,10);
        else if(strcmp(argv[i],"--inodes")==0 && i+1<argc) out->inodes=strtoull(argv[++i],NULL,10);
        else { usage(argv[0]); return -1; }
    }
    if(!out->image || out->size_kib<180 || out->size_kib>4096 || (out->size_kib%4)!=0 ||
        out->inodes<128 || out->inodes>512){ usage(argv[0]); return -1; }
    return 0;
}

// === Member 3 === layout computation
static void compute_layout(uint64_t size_kib, uint64_t inode_count, superblock_t* sb,uint64_t* itbl_blocks_out){
    uint64_t total_blocks=(size_kib*1024ull)/BS;
    uint64_t entries_per_block=BS/INODE_SIZE;
    uint64_t inode_table_blocks=(inode_count+entries_per_block-1)/entries_per_block;
    sb->magic=0x4D565346u; sb->version=1; sb->block_size=BS;
    sb->total_blocks=total_blocks; sb->inode_count=inode_count;
    sb->inode_bitmap_start=1; sb->inode_bitmap_blocks=1;
    sb->data_bitmap_start=2; sb->data_bitmap_blocks=1;
    sb->inode_table_start=3; sb->inode_table_blocks=inode_table_blocks;
    sb->data_region_start=sb->inode_table_start+sb->inode_table_blocks;
    sb->data_region_blocks=total_blocks-sb->data_region_start;
    sb->root_inode=ROOT_INO; sb->mtime_epoch=(uint64_t)time(NULL);
    sb->flags=0; sb->checksum=0;
    if(sb->data_region_blocks==0 || sb->data_region_start>=total_blocks){ fprintf(stderr,"Layout error\n"); exit(1);}
    *itbl_blocks_out=inode_table_blocks;
}

// === Member 3 === main builder logic
int main(int argc,char** argv){
    crc32_init();
    cli_t cli; if(parse_cli(argc,argv,&cli)!=0) return 2;
    superblock_t sb; uint64_t itbl_blocks=0;
    compute_layout(cli.size_kib,cli.inodes,&sb,&itbl_blocks);
    superblock_crc_finalize(&sb);
    uint64_t total_bytes=sb.total_blocks*BS;
    uint8_t* img=(uint8_t*)calloc(1,total_bytes); if(!img){ perror("calloc"); return 1; }

    uint8_t* p_super=img+0*BS;
    uint8_t* p_ibm=img+sb.inode_bitmap_start*BS;
    uint8_t* p_dbm=img+sb.data_bitmap_start*BS;
    uint8_t* p_itbl=img+sb.inode_table_start*BS;
    uint8_t* p_data=img+sb.data_region_start*BS;

    memcpy(p_super,&sb,sizeof(sb));
    bitmap_set(p_ibm,0); bitmap_set(p_dbm,0);

    inode_t root; memset(&root,0,sizeof(root));
    root.mode=0040000; root.links=2;
    root.uid=0; root.gid=0;
    root.size_bytes=BS;
    root.atime=root.mtime=root.ctime=(uint64_t)time(NULL);
    root.direct[0]=(uint32_t)(sb.data_region_start+0);
    root.proj_id=GROUP_ID;
    inode_crc_finalize(&root);
    memcpy(p_itbl+0*INODE_SIZE,&root,sizeof(root));

    dirent64_t dot,dotdot; memset(&dot,0,sizeof(dot));
    dot.inode_no=ROOT_INO; dot.type=2; strncpy(dot.name,".",sizeof(dot.name));
    dirent_checksum_finalize(&dot);
    memset(&dotdot,0,sizeof(dotdot));
    dotdot.inode_no=ROOT_INO; dotdot.type=2; strncpy(dotdot.name,"..",sizeof(dotdot.name));
    dirent_checksum_finalize(&dotdot);
    memcpy(p_data+0,&dot,sizeof(dot)); memcpy(p_data+sizeof(dot),&dotdot,sizeof(dotdot));

    FILE* f=fopen(cli.image,"wb"); if(!f){ perror("fopen"); free(img); return 1; }
    fwrite(img,1,total_bytes,f); fclose(f); free(img);
    fprintf(stderr,"MiniVSFS image created: %s (%" PRIu64 " blocks, %" PRIu64 " inodes)\n",
            cli.image,sb.total_blocks,sb.inode_count);

    return 0;
}