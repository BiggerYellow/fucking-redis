/* Hash Tables Implementation.
 *
 * This file implements in-memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto-resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DICT_H
#define __DICT_H

#include "mt19937-64.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#define DICT_OK 0
#define DICT_ERR 1

/* Unused arguments generate annoying warnings... */
#define DICT_NOTUSED(V) ((void) V)

typedef struct dictEntry {
    //对应的键
    void *key;
    //对应的值 可以是一个指针，也可以是数字类型
    union {
        void *val;
        uint64_t u64;
        int64_t s64;
        double d;
    } v;
    //指向下一个哈希表节点，形成链表
    struct dictEntry *next;
} dictEntry;

//dictType 实际上就是哈希算法
typedef struct dictType {
    //hash 方法，根据 key 计算哈希值
    uint64_t (*hashFunction)(const void *key);
    //复制 key
    void *(*keyDup)(void *privdata, const void *key);
    //复制 value
    void *(*valDup)(void *privdata, const void *obj);
    //key 比较
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);
    //销毁 key
    void (*keyDestructor)(void *privdata, void *key);
    //销毁 value
    void (*valDestructor)(void *privdata, void *obj);
    //允许扩容
    int (*expandAllowed)(size_t moreMem, double usedRatio);
} dictType;

/* This is our hash table structure. Every dictionary has two of this as we
 * implement incremental rehashing, for the old to the new table.
 * 这是我们的哈希表结构。
 * 当我们实现增量渐进式哈希时，每个字典都有两个这样的表，将旧表放到新表中。
 * */
typedef struct dictht {
    //哈希表数组
    dictEntry **table;
    //哈希表大小
    unsigned long size;
    //哈希表大小掩码，用于计算索引值
    //总是等于 size-1
    unsigned long sizemask;
    //该哈希表已有节点的数量
    unsigned long used;
} dictht;

typedef struct dict {
    //哈希算法
    dictType *type;
    //私有数据，用于不同类型的哈希算法的参数
    void *privdata;
    //两个哈希表，用两个的原因是 rehash 扩容缩容使用
    dictht ht[2];
    // rehash 进行到的索引值，当没有在 rehash 的时候，值为-1
    long rehashidx; /* rehashing not in progress if rehashidx == -1 */
    //rehash 暂停标志，如果大于 0 代表 rehash 暂停
    int16_t pauserehash; /* If >0 rehashing is paused (<0 indicates coding error) */
} dict;

/* If safe is set to 1 this is a safe iterator, that means, you can call
 * dictAdd, dictFind, and other functions against the dictionary even while
 * iterating. Otherwise it is a non safe iterator, and only dictNext()
 * should be called while iterating.
 * 字典迭代器
 * 如果 safe 属性的值为1，那么在迭代进行的过程中，程序仍然可以执行 dictAdd，dictFind 和其他函数对字典进行修改。
 * 如果 safe 不为1，那么程序只会调用 dictNext 对字典进行迭代，而不对字典进行修改
 * */
typedef struct dictIterator {
    //被迭代的字典
    dict *d;
    //正在被迭代的哈希表的索引
    long index;
    //正在被迭代的哈希表
    //当前迭代器是否安全
    int table, safe;
    //当前迭代到的节点指针
    //当前迭代节点的下一个节点（因为迭代器运作时，当前节点可能会被修改，必须保存下一个节点位置，避免指针丢失）
    dictEntry *entry, *nextEntry;
    /* unsafe iterator fingerprint for misuse detection.
     * 不安全迭代器，正在更新中的表的迭代器，使用指纹来标记
     * */
    unsigned long long fingerprint;
} dictIterator;

typedef void (dictScanFunction)(void *privdata, const dictEntry *de);
typedef void (dictScanBucketFunction)(void *privdata, dictEntry **bucketref);

/* This is the initial size of every hash table */
#define DICT_HT_INITIAL_SIZE     4

/* ------------------------------- Macros ------------------------------------*/
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata, (entry)->v.val)

#define dictSetVal(d, entry, _val_) do { \
    if ((d)->type->valDup) \
        (entry)->v.val = (d)->type->valDup((d)->privdata, _val_); \
    else \
        (entry)->v.val = (_val_); \
} while(0)

#define dictSetSignedIntegerVal(entry, _val_) \
    do { (entry)->v.s64 = _val_; } while(0)

#define dictSetUnsignedIntegerVal(entry, _val_) \
    do { (entry)->v.u64 = _val_; } while(0)

#define dictSetDoubleVal(entry, _val_) \
    do { (entry)->v.d = _val_; } while(0)

#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata, (entry)->key)

#define dictSetKey(d, entry, _key_) do { \
    if ((d)->type->keyDup) \
        (entry)->key = (d)->type->keyDup((d)->privdata, _key_); \
    else \
        (entry)->key = (_key_); \
} while(0)

#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata, key1, key2) : \
        (key1) == (key2))

#define dictHashKey(d, key) (d)->type->hashFunction(key)
#define dictGetKey(he) ((he)->key)
#define dictGetVal(he) ((he)->v.val)
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
#define dictGetDoubleVal(he) ((he)->v.d)
#define dictSlots(d) ((d)->ht[0].size+(d)->ht[1].size)
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)
#define dictIsRehashing(d) ((d)->rehashidx != -1)
#define dictPauseRehashing(d) (d)->pauserehash++
#define dictResumeRehashing(d) (d)->pauserehash--

/* If our unsigned long type can store a 64 bit number, use a 64 bit PRNG. */
#if ULONG_MAX >= 0xffffffffffffffff
#define randomULong() ((unsigned long) genrand64_int64())
#else
#define randomULong() random()
#endif

typedef enum {
    DICT_RESIZE_ENABLE,
    DICT_RESIZE_AVOID,
    DICT_RESIZE_FORBID,
} dictResizeEnable;

/* API */
dict *dictCreate(dictType *type, void *privDataPtr);
int dictExpand(dict *d, unsigned long size);
int dictTryExpand(dict *d, unsigned long size);
int dictAdd(dict *d, void *key, void *val);
dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing);
dictEntry *dictAddOrFind(dict *d, void *key);
int dictReplace(dict *d, void *key, void *val);
int dictDelete(dict *d, const void *key);
dictEntry *dictUnlink(dict *ht, const void *key);
void dictFreeUnlinkedEntry(dict *d, dictEntry *he);
void dictRelease(dict *d);
dictEntry * dictFind(dict *d, const void *key);
void *dictFetchValue(dict *d, const void *key);
int dictResize(dict *d);
dictIterator *dictGetIterator(dict *d);
dictIterator *dictGetSafeIterator(dict *d);
dictEntry *dictNext(dictIterator *iter);
void dictReleaseIterator(dictIterator *iter);
dictEntry *dictGetRandomKey(dict *d);
dictEntry *dictGetFairRandomKey(dict *d);
unsigned int dictGetSomeKeys(dict *d, dictEntry **des, unsigned int count);
void dictGetStats(char *buf, size_t bufsize, dict *d);
uint64_t dictGenHashFunction(const void *key, int len);
uint64_t dictGenCaseHashFunction(const unsigned char *buf, int len);
void dictEmpty(dict *d, void(callback)(void*));
void dictSetResizeEnabled(dictResizeEnable enable);
int dictRehash(dict *d, int n);
int dictRehashMilliseconds(dict *d, int ms);
void dictSetHashFunctionSeed(uint8_t *seed);
uint8_t *dictGetHashFunctionSeed(void);
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, dictScanBucketFunction *bucketfn, void *privdata);
uint64_t dictGetHash(dict *d, const void *key);
dictEntry **dictFindEntryRefByPtrAndHash(dict *d, const void *oldptr, uint64_t hash);

/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#ifdef REDIS_TEST
int dictTest(int argc, char *argv[], int accurate);
#endif

#endif /* __DICT_H */
