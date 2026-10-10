#include "video_handler_manager.h"
#include "rk_mpi_sys.h"
static int reference_count = 0;

int cam_video_handler_add_reference()
{
    int ret = 0;
    if (reference_count <= 0)
    {
        ret = RK_MPI_SYS_Init();
    }
    reference_count++;
    printf("handler reference [%d]\n", reference_count);
    return ret;
}

int cam_video_handler_del_reference()
{
    int ret = 0;
    reference_count--;
    printf("handler reference now [%d]\n", reference_count);
    if (reference_count <= 0)
    {
        ret = RK_MPI_SYS_Exit();
    }
    return ret;
}