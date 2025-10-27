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
#include <sys/stat.h>

// === Member 1 === constants
#define BS 4096u
#define INODE_SIZE 128u
#define GROUP_ID 5u
#define ROOT_INO 1u

// === Member 2 === CRC32
static uint32_t crc32_table[256];
static void crc32_init(void){
    uint32_t poly=0xEDB88320u;
    for(uint32_t i=0;i<256;i++){
        uint32_t c=i;
        for(int j=0;j<8;j++) c=(c&1)?(poly^(c>>1)):(c>>1);
        crc32_table[i]=c;
    }
}
static uint32_t crc32_compute(const void* data,size_t n){
    const uint8_t* p=(const uint8_t*)data;
    uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c=crc32_table[(c^p[i])&0xFFu]^(c>>8);
    return c^0xFFFFFFFFu;
}

// === Member 2 === structures
#pragma pack(push,1)
typedef struct {
    uint32_t magic,version,block_size;
    uint64_t total_blocks,inode_count;
    uint64_t inode_bitmap_start,inode_bitmap_blocks;
    uint64_t data_bitmap_start,data_bitmap_blocks;
    uint64_t inode_table_start,inode_table_blocks;
    uint64_t data_region_start,data_region_blocks;
    uint64_t root_inode,mtime_epoch;
    uint32_t flags,checksum;
} superblock_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    uint16_t mode,links;
    uint32_t uid,gid;
    uint64_t size_bytes,atime,mtime,ctime;
    uint32_t direct[12];
    uint32_t reserved_0,reserved_1,reserved_2;
    uint32_t proj_id,uid16_gid16;
    uint64_t xattr_ptr,inode_crc;
} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t)==INODE_SIZE,"inode size mismatch");

#pragma pack(push,1)
typedef struct {
    uint32_t inode_no;
    uint8_t type;
    char name[58];
    uint8_t checksum;
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t)==64,"dirent64 size mismatch");

// === Member 2 === checksum helpers
static void inode_crc_finalize(inode_t* ino){
    ino->inode_crc=0;
    uint32_t c=crc32_compute(ino,120);
    ino->inode_crc=(uint64_t)c;
}
static void dirent_checksum_finalize(dirent64_t* de){
    const uint8_t* p=(const uint8_t*)de;
    uint8_t x=0;
    for(int i=0;i<63;i++) x^=p[i];
    de->checksum=x;
}
static int validate_sb(const superblock_t* sb){
    if(sb->magic!=0x4D565346u) return -1;
    if(sb->version!=1) return -1;
    if(sb->block_size!=BS) return -1;
    if(sb->inode_bitmap_blocks!=1 || sb->data_bitmap_blocks!=1) return -1;
    if(sb->root_inode!=ROOT_INO) return -1;
    return 0;
}

// === Member 1 === file helpers
static void* load_file(const char* path,size_t* sz_out){
    FILE* f=fopen(path,"rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    void* buf=malloc(sz); if(!buf){fclose(f);return NULL;}
    if(fread(buf,1,sz,f)!=(size_t)sz){free(buf);fclose(f);return NULL;}
    fclose(f); *sz_out=(size_t)sz; return buf;
}
static int save_file(const char* path,const void* buf,size_t sz){
    FILE* f=fopen(path,"wb"); if(!f) return -1;
    size_t wr=fwrite(buf,1,sz,f); fclose(f);
    return (wr==sz)?0:-1;
}

// === Member 2 === bitmap helpers
static void bitmap_set(uint8_t* bm,uint64_t idx){
    bm[idx/8]|=(1u<<(idx%8));
}
static int bitmap_find_first_zero_and_set(uint8_t* bm,uint64_t nbits){
    for(uint64_t i=0;i<nbits;i++){
        uint8_t b=bm[i/8];
        if(((b>>(i%8))&1u)==0){
            bm[i/8]|=(1u<<(i%8));
            return (int)i;
        }
    }
    return -1;
}

// === Member 1 === CLI parser
typedef struct{const char* input,*output,*file;} cli_t;
static void usage(const char* prog){
    fprintf(stderr,"Usage: %s --input in.img --output out.img --file <file>\n",prog);
}
static int parse_cli(int argc,char** argv,cli_t* out){
    memset(out,0,sizeof(*out));
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--input")==0 && i+1<argc) out->input=argv[++i];
        else if(strcmp(argv[i],"--output")==0 && i+1<argc) out->output=argv[++i];
        else if(strcmp(argv[i],"--file")==0 && i+1<argc) out->file=argv[++i];
        else { usage(argv[0]); return -1; }
    }
    if(!out->input||!out->output||!out->file){ usage(argv[0]); return -1;}
    return 0;
}

// === Member 3 === main logic
int main(int argc,char** argv){
    crc32_init();
    cli_t cli; if(parse_cli(argc,argv,&cli)!=0) return 2;

    // Load FS image
    size_t img_sz=0;
    uint8_t* img=(uint8_t*)load_file(cli.input,&img_sz);
    if(!img){ perror("load input"); return 1; }
    if(img_sz<BS){ fprintf(stderr,"image too small\n"); free(img); return 1; }

    // Validate superblock
    superblock_t sb;
    memcpy(&sb,img,sizeof(sb));
    if(validate_sb(&sb)!=0){
        fprintf(stderr,"invalid superblock\n"); free(img); return 1;
    }

    // Locate regions
    uint8_t* p_ibm=img+sb.inode_bitmap_start*BS;
    uint8_t* p_dbm=img+sb.data_bitmap_start*BS;
    uint8_t* p_itbl=img+sb.inode_table_start*BS;

    // Load file to add
    size_t fs_sz=0;
    uint8_t* fbuf=(uint8_t*)load_file(cli.file,&fs_sz);
    if(!fbuf){
        fprintf(stderr,"cannot open file '%s'\n",cli.file);
        free(img); return 1;
    }

    // Find free inode
    int free_ino_idx0=-1;
    for(uint64_t i=0;i<sb.inode_count;i++){
        if(((p_ibm[i/8]>>(i%8))&1u)==0){ free_ino_idx0=(int)i; break; }
    }
    if(free_ino_idx0<0){
        fprintf(stderr,"no free inodes\n"); free(fbuf); free(img); return 1;
    }
    bitmap_set(p_ibm,(uint64_t)free_ino_idx0);

    // Fill inode
    inode_t ino; memset(&ino,0,sizeof(ino));
    ino.mode=0100000; ino.links=1; ino.uid=0; ino.gid=0;
    uint64_t max_bytes=12ull*BS;
    uint64_t write_bytes=fs_sz>max_bytes?max_bytes:fs_sz;
    ino.size_bytes=write_bytes;
    ino.atime=ino.mtime=ino.ctime=(uint64_t)time(NULL);
    ino.proj_id=GROUP_ID;

    // Allocate data blocks
    uint64_t blocks_needed=(write_bytes+BS-1)/BS;
    if(fs_sz>max_bytes)
        fprintf(stderr,"warning: file truncated to %" PRIu64 " bytes\n",max_bytes);

    for(uint64_t b=0;b<blocks_needed;b++){
        int db=bitmap_find_first_zero_and_set(p_dbm,sb.data_region_blocks);
        if(db<0){
            fprintf(stderr,"no free data blocks\n"); free(fbuf); free(img); return 1;
        }
        uint64_t abs_block=sb.data_region_start+(uint64_t)db;
        ino.direct[b]=(uint32_t)abs_block;
        uint64_t off=b*BS;
        uint64_t chunk=(write_bytes-off)<BS?(write_bytes-off):BS;
        memcpy(img+abs_block*BS,fbuf+off,chunk);
    }
    inode_crc_finalize(&ino);
    memcpy(p_itbl+(size_t)free_ino_idx0*INODE_SIZE,&ino,sizeof(ino));

    // Update root directory
    inode_t root; memcpy(&root,p_itbl+0*INODE_SIZE,sizeof(root));
    uint64_t root_block=root.direct[0];
    uint8_t* dirblk=img+root_block*BS;
    dirent64_t de; int placed=0;
    for(size_t off=0; off+sizeof(dirent64_t)<=BS; off+=sizeof(dirent64_t)){
        memcpy(&de,dirblk+off,sizeof(de));
        if(de.inode_no==0){
            memset(&de,0,sizeof(de));
            de.inode_no=(uint32_t)(free_ino_idx0+1);
            de.type=1;
            const char* base=cli.file;
            const char* slash=strrchr(base,'/');
            if(slash) base=slash+1;
            strncpy(de.name,base,sizeof(de.name));
            de.name[sizeof(de.name)-1]='\0';
            dirent_checksum_finalize(&de);
            memcpy(dirblk+off,&de,sizeof(de));
            placed=1;
            break;
        }
    }
    if(!placed){
        fprintf(stderr,"root directory full\n"); free(fbuf); free(img); return 1;
    }

    // Update root inode
    root.links+=1;
    inode_crc_finalize(&root);
    memcpy(p_itbl+0*INODE_SIZE,&root,sizeof(root));

    // Update superblock time
    sb.mtime_epoch=(uint64_t)time(NULL);
    memcpy(img,&sb,sizeof(sb));

    // Save new image
    int rc=save_file(cli.output,img,img_sz);
    if(rc!=0){
        fprintf(stderr,"failed to save output\n"); free(fbuf); free(img); return 1;
    }
    fprintf(stderr,"Added '%s' as inode #%d, wrote %" PRIu64 " bytes into %" PRIu64 " blocks\n",
            cli.file,free_ino_idx0+1,ino.size_bytes,blocks_needed);
    free(fbuf);
    free(img);

    return 0;
}