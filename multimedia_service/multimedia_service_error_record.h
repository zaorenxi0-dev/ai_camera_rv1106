#ifndef __MULTIMEDIA_SERVICE_ERROR_RECORD_H__
#define __MULTIMEDIA_SERVICE_ERROR_RECORD_H__

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <iostream>


using namespace std;


class SvcErrorRecord
{
  public:
    SvcErrorRecord()
    {
        mInited       = false;
        mIsError      = false;
        mLatest       = 0;
        mErrorCount   = 0;
        mSuccessCount = 0;
        mIndex = 0;
    };
    ~SvcErrorRecord(){};

    bool isInited()
    {
        return mInited;
    };

    /**
     * @brief       初始化
     * @param[in]   isError：错误标志，true表示错误，false表示正常
     * @param[in]   timestamp：时间戳
     * @return      无
     */
    void init(bool isError, uint64_t timestamp, int index = 0, char *name = NULL)
    {
        if (mInited)
        {
            return;
        }

        printf("isError= [%d] timestamp = [%llu] name = [%s] index = [%d]",
                   isError, timestamp, name != NULL ? name : "noName", mIndex);
        mIsError      = isError;
        mLatest       = timestamp;
        mErrorCache   = isError;
        mTimeCache    = timestamp;
        mErrorCount   = 0;
        mSuccessCount = 0;
        mInited       = true;
        if (name)
        {
            mName = name;
        }
        mIndex = index;
    };

    void setName(const char *name, int index = 0)
    {
        mName = name;
        mIndex = index;
    }

    bool isError()
    {
        return mIsError;
    };

    /**
     * @brief       更新错误标志
     * @param[in]   isError：错误标志，true表示错误，false表示正常
     * @param[in]   timestamp：时间戳
     * @return      无
     */
    void updateError(bool isError, uint64_t timestamp)
    {
        printf("name [%s:%d] isError = [%d] timestamp [%llu]", mName.c_str(),
                   mIndex,
                   isError, timestamp);
        mIsError = isError;
        mLatest  = timestamp;
    };

    /**
     * @brief       获取最后一次更新错误标记时的时间戳
     * @return      时间戳
     */
    uint64_t getLatest()
    {
        return mLatest;
    };

    /**
     * @brief       缓存临时错误标志
     * @param[in]   isError：错误标志，true表示错误，false表示正常
     * @param[in]   timestamp：时间戳
     * @return      无
     */
    void cacheError(bool isError, uint64_t timestamp)
    {
        printf("name [%s:%d] isError = [%d] timestamp [%llu]", mName.c_str(),
                   mIndex,
                   isError, timestamp);
        mErrorCache = isError;
        mTimeCache  = timestamp;
    };

    /**
     * @brief       获取临时错误标志
     * @return
     */
    bool getCacheError()
    {
        return mErrorCache;
    };

    /**
     * @brief       获取最后一次更新临时错误标记时的时间戳
     * @return      时间戳
     */
    uint64_t getCacheTime()
    {
        return mTimeCache;
    };

    int getErrorCount()
    {
        return mErrorCount;
    };

    int getSuccessCount()
    {
        return mSuccessCount;
    };

    void clearErrorCount()
    {
        mErrorCount = 0;
    };

    void clearSuccessCount()
    {
        mSuccessCount = 0;
    };

    void tickErrorCount()
    {
        mErrorCount++;
    };

    void tickSuccessCount()
    {
        mSuccessCount++;
    };

  private:
    bool     mInited;       // 初始化标志，true表示已初始化，否则表示未初始化
    bool     mIsError;      // 错误标志，true表示处于错误状态，否则表示处于正常状态
    uint64_t mLatest;       // 最后一次更新错误标志时的时间戳，单位自行确定
    bool     mErrorCache;   // 临时错误标志，true表示处于错误状态，否则表示处于正常状态
    uint64_t mTimeCache;    // 最后一次更新临时错误标志时的时间戳，单位自行确定
    int      mErrorCount;   // 出错的次数
    int      mSuccessCount; // 正常的次数
    std::string mName;
    int      mIndex;
};





















#endif