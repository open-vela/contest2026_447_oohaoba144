#include <nuttx/config.h>
#include <nuttx/fs/fs.h>
#include "velaguard_nor.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static struct mtd_geometry_s geometry={512,4096,1024,{0}};
static struct mtd_dev_s lower;
static struct inode node;
static struct mtd_dev_s *proxy;
static int find_error,geometry_error,register_error,finds,releases,registers;
static int sequence,clean_order,write_order,write_calls,read_calls,erase_calls;
static uintptr_t clean_start,clean_end;
static const uint8_t *last_buffer;
static off_t last_start;
static size_t last_count;
static ssize_t result=2;
static uint8_t buffer[2048];

int find_mtddriver(const char *path,struct inode **out)
{
  assert(!strcmp(path,VG_NOR_SOURCE));finds++;
  if(find_error)return find_error;
  *out=&node;return 0;
}
int close_mtddriver(struct inode *inode)
{
  assert(inode==&node);releases++;return 0;
}
int register_mtddriver(const char *path,struct mtd_dev_s *mtd,mode_t mode,void *priv)
{
  assert(!strcmp(path,VG_NOR_DEVICE));assert(mode==0600);(void)priv;registers++;
  if(register_error)return register_error;
  proxy=mtd;return 0;
}
void up_clean_dcache(uintptr_t start,uintptr_t end)
{
  clean_start=start;clean_end=end;clean_order=++sequence;
}
static int control(struct mtd_dev_s *dev,int cmd,uintptr_t arg)
{
  assert(dev==&lower);
  if(cmd!=MTDIOC_GEOMETRY)return -ENOTTY;
  if(geometry_error)return geometry_error;
  *(struct mtd_geometry_s *)arg=geometry;return 0;
}
static ssize_t blocks_write(struct mtd_dev_s *dev,off_t start,size_t n,const uint8_t *buf)
{
  assert(dev==&lower);write_calls++;write_order=++sequence;
  last_start=start;last_count=n;last_buffer=buf;return result;
}
static ssize_t bytes_write(struct mtd_dev_s *dev,off_t start,size_t n,const uint8_t *buf)
{return blocks_write(dev,start,n,buf);}
static ssize_t read_data(struct mtd_dev_s *dev,off_t start,size_t n,uint8_t *buf)
{
  assert(dev==&lower);read_calls++;last_start=start;last_count=n;last_buffer=buf;return result;
}
static int erase_data(struct mtd_dev_s *dev,off_t start,size_t n)
{
  assert(dev==&lower);erase_calls++;last_start=start;last_count=n;return (int)result;
}
int main(void)
{
  int before;
  lower.bwrite=blocks_write;lower.write=bytes_write;lower.bread=read_data;
  lower.read=read_data;lower.erase=erase_data;lower.ioctl=control;node.u.i_mtd=&lower;
  find_error=-ENOENT;assert(vg_nor_prepare()==-ENOENT && releases==0);find_error=0;
  geometry_error=-EIO;assert(vg_nor_prepare()==-EIO && releases==1);geometry_error=0;
  geometry.blocksize=0;assert(vg_nor_prepare()==-EINVAL);geometry.blocksize=512;
  geometry.erasesize=513;assert(vg_nor_prepare()==-EINVAL);geometry.erasesize=4096;
  geometry.neraseblocks=0;assert(vg_nor_prepare()==-EINVAL);
  geometry.blocksize=1;geometry.erasesize=UINT32_MAX;geometry.neraseblocks=UINT32_MAX;
  assert(vg_nor_prepare()==-EOVERFLOW);
  geometry.blocksize=512;geometry.erasesize=4096;geometry.neraseblocks=1024;
  register_error=-EEXIST;before=releases;assert(vg_nor_prepare()==-EEXIST && releases==before+1);register_error=0;
  assert(proxy==NULL);assert(vg_nor_prepare()==0 && proxy!=NULL);
  before=finds;assert(vg_nor_prepare()==0 && finds==before && registers==2);
  assert(proxy->bwrite(proxy,4,3,buffer)==2);
  assert(clean_order<write_order && clean_start==(uintptr_t)buffer && clean_end==(uintptr_t)buffer+1536);
  assert(last_start==4 && last_count==3 && last_buffer==buffer);
  result=-EIO;assert(proxy->bwrite(proxy,1,1,buffer)==-EIO && clean_order<write_order);
  before=write_calls;assert(proxy->bwrite(proxy,-1,1,buffer)==-EINVAL);
  assert(proxy->bwrite(proxy,8192,1,buffer)==-EINVAL);
  assert(proxy->bwrite(proxy,0,SIZE_MAX/512+1,buffer)==-EOVERFLOW);
  assert(proxy->bwrite(proxy,0,1,(const uint8_t *)(UINTPTR_MAX-15))==-EOVERFLOW);
  assert(proxy->bwrite(proxy,0,1,NULL)==-EINVAL);
  assert(proxy->bwrite(proxy,8192,0,NULL)==0 && write_calls==before);
  result=7;assert(proxy->write(proxy,15,17,buffer+1)==7);
  assert(clean_start==(uintptr_t)(buffer+1) && clean_end==(uintptr_t)(buffer+18) && clean_order<write_order);
  assert(last_start==15 && last_count==17);
  before=write_calls;assert(proxy->write(proxy,4194300,17,buffer)==-EINVAL && write_calls==before);
  result=-ENOSPC;assert(proxy->write(proxy,0,1,buffer)==-ENOSPC);
  before=sequence;result=1;assert(proxy->bread(proxy,2,3,buffer)==1 && sequence==before);
  assert(proxy->read(proxy,5,9,buffer)==1 && last_start==5 && last_count==9);
  assert(proxy->erase(proxy,2,1)==1 && erase_calls==1);
  assert(proxy->erase(proxy,1024,1)==-EINVAL && erase_calls==1);
  assert(proxy->read(proxy,4194304,1,buffer)==-EINVAL);
  assert(proxy->ioctl(proxy,99,0)==-ENOTTY);
  puts("PASS NOR proxy: prepare failures/retry/idempotence, exact clean-before-write, block/byte write, bounds/overflow, error/short-write passthrough");
  return 0;
}
