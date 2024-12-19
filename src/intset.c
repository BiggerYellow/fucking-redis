/*
 * Copyright (c) 2009-2012, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intset.h"
#include "zmalloc.h"
#include "endianconv.h"
#include "redisassert.h"

/* Note that these encodings are ordered, so:
 * INTSET_ENC_INT16 < INTSET_ENC_INT32 < INTSET_ENC_INT64.
 * 请注意这些编码是有序的，如下： 16 < 32 < 64
 * */
#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

/* Return the required encoding for the provided value.
 * 返回所提供值需要的编码
 * */
static uint8_t _intsetValueEncoding(int64_t v) {
    //若该值小于 32位的最小值 或 大于32位的最大值 则为 64位
    if (v < INT32_MIN || v > INT32_MAX)
        return INTSET_ENC_INT64;
    else if (v < INT16_MIN || v > INT16_MAX) //若该值小于16位的最小值 或 大于16位的最大值 则为 32位
        return INTSET_ENC_INT32;
    else
        return INTSET_ENC_INT16; //否则就是 16位
}

/* Return the value at pos, given an encoding.
 * 返回给定编码的 pos 处的值
 * */
static int64_t _intsetGetEncoded(intset *is, int pos, uint8_t enc) {
    int64_t v64;
    int32_t v32;
    int16_t v16;

    if (enc == INTSET_ENC_INT64) {
        memcpy(&v64,((int64_t*)is->contents)+pos,sizeof(v64));
        memrev64ifbe(&v64);
        return v64;
    } else if (enc == INTSET_ENC_INT32) {
        memcpy(&v32,((int32_t*)is->contents)+pos,sizeof(v32));
        memrev32ifbe(&v32);
        return v32;
    } else {
        memcpy(&v16,((int16_t*)is->contents)+pos,sizeof(v16));
        memrev16ifbe(&v16);
        return v16;
    }
}

/* Return the value at pos, using the configured encoding.
 * 使用配置的编码返回指定位置的值
 * */
static int64_t _intsetGet(intset *is, int pos) {
    return _intsetGetEncoded(is,pos,intrev32ifbe(is->encoding));
}

/* Set the value at pos, using the configured encoding.
 * 将值插入到指定位置，并使用配置的编码
 * */
static void _intsetSet(intset *is, int pos, int64_t value) {
    uint32_t encoding = intrev32ifbe(is->encoding);

    if (encoding == INTSET_ENC_INT64) {
        ((int64_t*)is->contents)[pos] = value;
        memrev64ifbe(((int64_t*)is->contents)+pos);
    } else if (encoding == INTSET_ENC_INT32) {
        ((int32_t*)is->contents)[pos] = value;
        memrev32ifbe(((int32_t*)is->contents)+pos);
    } else {
        ((int16_t*)is->contents)[pos] = value;
        memrev16ifbe(((int16_t*)is->contents)+pos);
    }
}

/* Create an empty intset.
 * 创建空的整数集合
 * */
intset *intsetNew(void) {
    //分配内存
    intset *is = zmalloc(sizeof(intset));
    //初始化整数类型为 int16    intset的所有成员存储方式都采用小端序
    is->encoding = intrev32ifbe(INTSET_ENC_INT16);
    //初始化集合长度为 0
    is->length = 0;
    return is;
}

/* Resize the intset
 * 整数集合扩容
 * */
static intset *intsetResize(intset *is, uint32_t len) {
    //计算最终大小 扩容后的数量*最新编码
    uint64_t size = (uint64_t)len*intrev32ifbe(is->encoding);
    assert(size <= SIZE_MAX - sizeof(intset));
    //重新分配内存 intset原始大小 + 最新数组大小
    is = zrealloc(is,sizeof(intset)+size);
    return is;
}

/* Search for the position of "value". Return 1 when the value was found and
 * sets "pos" to the position of the value within the intset. Return 0 when
 * the value is not present in the intset and sets "pos" to the position
 * where "value" can be inserted.
 * 搜索value的位置。如果值被找到则返回1并将值在集合中的位置设置到 pos 中。
 * 如果值不存在于集合中返回0，并值可以被插入的位置设置到 pos 中
 * */
static uint8_t intsetSearch(intset *is, int64_t value, uint32_t *pos) {
    //初始化变量
    //min = 0  最小
    //max = length-1 最大
    //mid = -1 中间位置
    int min = 0, max = intrev32ifbe(is->length)-1, mid = -1;
    //cur = -1 当前位置
    int64_t cur = -1;

    /* The value can never be found when the set is empty
     * 如果集合为空则值永远不会被发现
     * */
    //集合长度为0 代表空集合
    if (intrev32ifbe(is->length) == 0) {
        //pos设置为0 表明可以插入到第一个位置
        if (pos) *pos = 0;
        //插入失败返回0
        return 0;
    } else {

        /* Check for the case where we know we cannot find the value,
         * but do know the insert position.
         * 检查我们找不到该值但是知道要插入哪里的情况
         * */
        //如果该值大于 最大索引对应的值
        if (value > _intsetGet(is,max)) {
            //说明待插入位置为最后
            if (pos) *pos = intrev32ifbe(is->length);
            //返回0
            return 0;
        } else if (value < _intsetGet(is,0)) { //如果该值小于最小索引对应的值
            //说明待插入位置为最开始
            if (pos) *pos = 0;
            //返回0
            return 0;
        }
    }

    //二分查找搜索找到对应的位置
    while(max >= min) {
        //取中间位置
        mid = ((unsigned int)min + (unsigned int)max) >> 1;
        //获取中间位置对应的值
        cur = _intsetGet(is,mid);
        //如果待插入值大于当前值  缩小左边
        if (value > cur) {
            min = mid+1;
        } else if (value < cur) {//如果待插入值小于当前值 缩小右边
            max = mid-1;
        } else {
            break;
        }
    }

    //如果当前值 等于 待插入值
    if (value == cur) {
        //设置位置为mid
        if (pos) *pos = mid;
        //返回1
        return 1;
    } else {
        //不存在则说明要插入到 二分查找排序的最小位置
        if (pos) *pos = min;
        //返回0
        return 0;
    }
}

/* Upgrades the intset to a larger encoding and inserts the given integer.
 * 升级整数集合到更大的编码 插入给定的整数
 * */
static intset *intsetUpgradeAndAdd(intset *is, int64_t value) {
    //当前整数集合编码
    uint8_t curenc = intrev32ifbe(is->encoding);
    //待插入值的编码
    uint8_t newenc = _intsetValueEncoding(value);
    //当前整数集合数量
    int length = intrev32ifbe(is->length);
    //拼接位置  如果小于0放在前面 如果大于0放在后面
    int prepend = value < 0 ? 1 : 0;

    /* First set new encoding and resize
     * 首先设置新编码并扩容
     * */
    //设置新编码
    is->encoding = intrev32ifbe(newenc);
    //数量扩容加1
    is = intsetResize(is,intrev32ifbe(is->length)+1);

    /* Upgrade back-to-front so we don't overwrite values.
     * Note that the "prepend" variable is used to make sure we have an empty
     * space at either the beginning or the end of the intset.
     * 从后到前升级，这样我们就不会覆盖
     * 请注意 prepend 比那里用来确保我们有空余空间在整数集的开头或结尾
     * */
    //递归处理 将原有整数插入到扩容后的集合中，需要注意将待插入索引空出来
    while(length--)
        //将整数插入到指定位置
        _intsetSet(is,length+prepend,_intsetGetEncoded(is,length,curenc));

    /* Set the value at the beginning or the end.
     * 将值插入到开头或者末尾
     * */
    //如果 prepend为1插入到开头，为0插入到末尾
    if (prepend)
        _intsetSet(is,0,value);
    else
        _intsetSet(is,intrev32ifbe(is->length),value);
    //更新整数集合大小 加1
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);
    return is;
}

static void intsetMoveTail(intset *is, uint32_t from, uint32_t to) {
    //*src 起始值
    //*dst 目标值
    void *src, *dst;
    //from 到 length 的字节数
    uint32_t bytes = intrev32ifbe(is->length)-from;
    //当前编码
    uint32_t encoding = intrev32ifbe(is->encoding);

    if (encoding == INTSET_ENC_INT64) {
        //真正的起始值
        src = (int64_t*)is->contents+from;
        //真正的模板值
        dst = (int64_t*)is->contents+to;
        //增加字节大小
        bytes *= sizeof(int64_t);
    } else if (encoding == INTSET_ENC_INT32) {
        src = (int32_t*)is->contents+from;
        dst = (int32_t*)is->contents+to;
        bytes *= sizeof(int32_t);
    } else {
        src = (int16_t*)is->contents+from;
        dst = (int16_t*)is->contents+to;
        bytes *= sizeof(int16_t);
    }
    //结构移动到末尾
    memmove(dst,src,bytes);
}

/* Insert an integer in the intset
 * 向整数集合中插入一个整数
 * */
intset *intsetAdd(intset *is, int64_t value, uint8_t *success) {
    //获取该值对应的编码
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;
    if (success) *success = 1;

    /* Upgrade encoding if necessary. If we need to upgrade, we know that
     * this value should be either appended (if > 0) or prepended (if < 0),
     * because it lies outside the range of existing values.
     * 如果需要的话升级编码。如果我们需要升级，我们要知道 这个值应该被附加（如果>0）或加在前面（如果<0），因为它位于现有值的范围之外
     * */
    //如果待插入值的编码大于当前整数集合编码
    if (valenc > intrev32ifbe(is->encoding)) {
        /* This always succeeds, so we don't need to curry *success.
         * 他总是成功，所以我们不需要追求 *success
         * */
        return intsetUpgradeAndAdd(is,value);
    } else {
        /* Abort if the value is already present in the set.
         * This call will populate "pos" with the right position to insert
         * the value when it cannot be found.
         * 如果该值已经存在于集合中则终止。
         * 这个调用将用正确的位置填充 pos 以便在找不到值时插入值
         * */
        if (intsetSearch(is,value,&pos)) {
            //找到则说明不用插入直接返回 说明插入未成功
            if (success) *success = 0;
            return is;
        }

        //该值不存在，首先扩容集合数量加1
        is = intsetResize(is,intrev32ifbe(is->length)+1);
        //如果待插入位置在中间 将待插入位置的值全部移动到末尾
        if (pos < intrev32ifbe(is->length)) intsetMoveTail(is,pos,pos+1);
    }

    //将值插入到指定位置
    _intsetSet(is,pos,value);
    //更新长度
    is->length = intrev32ifbe(intrev32ifbe(is->length)+1);
    return is;
}

/* Delete integer from intset
 * 从整数列表中删除整数值
 * */
intset *intsetRemove(intset *is, int64_t value, int *success) {
    //确定待删除值的编码
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;
    //成功标识 默认为0 失败
    if (success) *success = 0;

    //待删除值编码 小于等于 整数集合编码  且  该值存在于整数集合中
    if (valenc <= intrev32ifbe(is->encoding) && intsetSearch(is,value,&pos)) {
        //取出集合数量
        uint32_t len = intrev32ifbe(is->length);

        /* We know we can delete */
        //此时表明可以删除  设置删除标识为1
        if (success) *success = 1;

        /* Overwrite value with tail and update length
         * 用尾部覆盖值 且 更新长度
         * */
        //如果待删除值的位置 在集合中  直接用 pos+1 位置的值覆盖 pos位置
        if (pos < (len-1)) intsetMoveTail(is,pos+1,pos);
        //整数集合大小减1
        is = intsetResize(is,len-1);
        //更新结构长度字段
        is->length = intrev32ifbe(len-1);
    }
    return is;
}

/* Determine whether a value belongs to this set
 * 确定该值是否属于该集合
 * */
uint8_t intsetFind(intset *is, int64_t value) {
    //确定该值对应的编码
    uint8_t valenc = _intsetValueEncoding(value);
    //保证该类型要小于集合中的类型  且 集合中可以找到该值
    return valenc <= intrev32ifbe(is->encoding) && intsetSearch(is,value,NULL);
}

/* Return random member
 * 返回随机值
 * */
int64_t intsetRandom(intset *is) {
    uint32_t len = intrev32ifbe(is->length);
    assert(len); /* avoid division by zero on corrupt intset payload. */
    return _intsetGet(is,rand()%len);
}

/* Get the value at the given position. When this position is
 * out of range the function returns 0, when in range it returns 1.
 * 返回指定位置的值。
 * 如果位置超过集合长度返回 0 ，在范围内返回 1
 * */
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value) {
    //是否越界
    if (pos < intrev32ifbe(is->length)) {
        //查询索引对应的值
        *value = _intsetGet(is,pos);
        return 1;
    }
    return 0;
}

/* Return intset length
 * 返回整数集合的长度
 * */
uint32_t intsetLen(const intset *is) {
    return intrev32ifbe(is->length);
}

/* Return intset blob size in bytes.
 * 返回集合字节数
 * */
size_t intsetBlobLen(intset *is) {
    return sizeof(intset)+(size_t)intrev32ifbe(is->length)*intrev32ifbe(is->encoding);
}

/* Validate the integrity of the data structure.
 * when `deep` is 0, only the integrity of the header is validated.
 * when `deep` is 1, we make sure there are no duplicate or out of order records. */
int intsetValidateIntegrity(const unsigned char *p, size_t size, int deep) {
    intset *is = (intset *)p;
    /* check that we can actually read the header. */
    if (size < sizeof(*is))
        return 0;

    uint32_t encoding = intrev32ifbe(is->encoding);

    size_t record_size;
    if (encoding == INTSET_ENC_INT64) {
        record_size = INTSET_ENC_INT64;
    } else if (encoding == INTSET_ENC_INT32) {
        record_size = INTSET_ENC_INT32;
    } else if (encoding == INTSET_ENC_INT16){
        record_size = INTSET_ENC_INT16;
    } else {
        return 0;
    }

    /* check that the size matchies (all records are inside the buffer). */
    uint32_t count = intrev32ifbe(is->length);
    if (sizeof(*is) + count*record_size != size)
        return 0;

    /* check that the set is not empty. */
    if (count==0)
        return 0;

    if (!deep)
        return 1;

    /* check that there are no dup or out of order records. */
    int64_t prev = _intsetGet(is,0);
    for (uint32_t i=1; i<count; i++) {
        int64_t cur = _intsetGet(is,i);
        if (cur <= prev)
            return 0;
        prev = cur;
    }

    return 1;
}

#ifdef REDIS_TEST
#include <sys/time.h>
#include <time.h>

#if 0
static void intsetRepr(intset *is) {
    for (uint32_t i = 0; i < intrev32ifbe(is->length); i++) {
        printf("%lld\n", (uint64_t)_intsetGet(is,i));
    }
    printf("\n");
}

static void error(char *err) {
    printf("%s\n", err);
    exit(1);
}
#endif

static void ok(void) {
    printf("OK\n");
}

static long long usec(void) {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
}

static intset *createSet(int bits, int size) {
    uint64_t mask = (1<<bits)-1;
    uint64_t value;
    intset *is = intsetNew();

    for (int i = 0; i < size; i++) {
        if (bits > 32) {
            value = (rand()*rand()) & mask;
        } else {
            value = rand() & mask;
        }
        is = intsetAdd(is,value,NULL);
    }
    return is;
}

static void checkConsistency(intset *is) {
    for (uint32_t i = 0; i < (intrev32ifbe(is->length)-1); i++) {
        uint32_t encoding = intrev32ifbe(is->encoding);

        if (encoding == INTSET_ENC_INT16) {
            int16_t *i16 = (int16_t*)is->contents;
            assert(i16[i] < i16[i+1]);
        } else if (encoding == INTSET_ENC_INT32) {
            int32_t *i32 = (int32_t*)is->contents;
            assert(i32[i] < i32[i+1]);
        } else {
            int64_t *i64 = (int64_t*)is->contents;
            assert(i64[i] < i64[i+1]);
        }
    }
}

#define UNUSED(x) (void)(x)
int intsetTest(int argc, char **argv, int accurate) {
    uint8_t success;
    int i;
    intset *is;
    srand(time(NULL));

    UNUSED(argc);
    UNUSED(argv);
    UNUSED(accurate);

    printf("Value encodings: "); {
        assert(_intsetValueEncoding(-32768) == INTSET_ENC_INT16);
        assert(_intsetValueEncoding(+32767) == INTSET_ENC_INT16);
        assert(_intsetValueEncoding(-32769) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(+32768) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(-2147483648) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(+2147483647) == INTSET_ENC_INT32);
        assert(_intsetValueEncoding(-2147483649) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(+2147483648) == INTSET_ENC_INT64);
        assert(_intsetValueEncoding(-9223372036854775808ull) ==
                    INTSET_ENC_INT64);
        assert(_intsetValueEncoding(+9223372036854775807ull) ==
                    INTSET_ENC_INT64);
        ok();
    }

    printf("Basic adding: "); {
        is = intsetNew();
        is = intsetAdd(is,5,&success); assert(success);
        is = intsetAdd(is,6,&success); assert(success);
        is = intsetAdd(is,4,&success); assert(success);
        is = intsetAdd(is,4,&success); assert(!success);
        ok();
        zfree(is);
    }

    printf("Large number of random adds: "); {
        uint32_t inserts = 0;
        is = intsetNew();
        for (i = 0; i < 1024; i++) {
            is = intsetAdd(is,rand()%0x800,&success);
            if (success) inserts++;
        }
        assert(intrev32ifbe(is->length) == inserts);
        checkConsistency(is);
        ok();
        zfree(is);
    }

    printf("Upgrade from int16 to int32: "); {
        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        assert(intsetFind(is,32));
        assert(intsetFind(is,65535));
        checkConsistency(is);
        zfree(is);

        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,-65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        assert(intsetFind(is,32));
        assert(intsetFind(is,-65535));
        checkConsistency(is);
        ok();
        zfree(is);
    }

    printf("Upgrade from int16 to int64: "); {
        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,32));
        assert(intsetFind(is,4294967295));
        checkConsistency(is);
        zfree(is);

        is = intsetNew();
        is = intsetAdd(is,32,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT16);
        is = intsetAdd(is,-4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,32));
        assert(intsetFind(is,-4294967295));
        checkConsistency(is);
        ok();
        zfree(is);
    }

    printf("Upgrade from int32 to int64: "); {
        is = intsetNew();
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        is = intsetAdd(is,4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,65535));
        assert(intsetFind(is,4294967295));
        checkConsistency(is);
        zfree(is);

        is = intsetNew();
        is = intsetAdd(is,65535,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT32);
        is = intsetAdd(is,-4294967295,NULL);
        assert(intrev32ifbe(is->encoding) == INTSET_ENC_INT64);
        assert(intsetFind(is,65535));
        assert(intsetFind(is,-4294967295));
        checkConsistency(is);
        ok();
        zfree(is);
    }

    printf("Stress lookups: "); {
        long num = 100000, size = 10000;
        int i, bits = 20;
        long long start;
        is = createSet(bits,size);
        checkConsistency(is);

        start = usec();
        for (i = 0; i < num; i++) intsetSearch(is,rand() % ((1<<bits)-1),NULL);
        printf("%ld lookups, %ld element set, %lldusec\n",
               num,size,usec()-start);
        zfree(is);
    }

    printf("Stress add+delete: "); {
        int i, v1, v2;
        is = intsetNew();
        for (i = 0; i < 0xffff; i++) {
            v1 = rand() % 0xfff;
            is = intsetAdd(is,v1,NULL);
            assert(intsetFind(is,v1));

            v2 = rand() % 0xfff;
            is = intsetRemove(is,v2,NULL);
            assert(!intsetFind(is,v2));
        }
        checkConsistency(is);
        ok();
        zfree(is);
    }

    return 0;
}
#endif
