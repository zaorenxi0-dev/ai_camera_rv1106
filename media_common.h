#ifndef __MEDIA_COMMON_H__
#define __MEDIA_COMMON_H__

#ifdef __cplusplus
extern "C"
{
#endif

    /**@enum MUTIMEDIA_RUN_STATUS
     * @brief 多媒体程序中功能的运行状态
     */
    typedef enum
    {
        RUN_FALSE = 0, ///< 初始化未完成，无法使用
        RUN_TRUE,      ///< 可正常使用
    } MUTIMEDIA_RUN_STATUS;


#ifdef __cplusplus
}
#endif





#endif