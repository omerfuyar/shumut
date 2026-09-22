#define SHUC_NO_RUN_LOG
#define SHU_IMPLEMENTATION
#include "../../shu/shu.h"
#include "../../shuild/shuild.h"

#ifdef _WIN32
#define LINUX_SUFFIX ""
#else
#define LINUX_SUFFIX ".ignore"
#endif

void ShuildExample(const char *name)
{
    char buffer[64] = {0};

    snprintf(buffer, sizeof(buffer), "%s%s", name, LINUX_SUFFIX);
    SHU_ModuleBegin(buffer, NULL);

    snprintf(buffer, sizeof(buffer), "%s.c", name);
    SHU_ModuleAddSourceFile(buffer);

    SHU_ModuleCompile(NULL, SHUModuleType_Executable);
}

int main(int argc, char **argv)
{
    SHU_CompilerTryConfigure("gcc");
    SHU_UtilAutomate(argc, argv);

    SHU_CompilerAddFlags(SHUM_FLAGS_WARNING_LOW);
    SHU_CompilerAddFlags("-Wno-unused-function -Wno-format-truncation -Wno-implicit-fallthrough" SHUM_FLAGS_DEBUG SHUM_FLAGS_STANDARD_C23);
    SHU_CompilerAddDefinitions("SHU_IMPLEMENTATION", NULL);

    ShuildExample("1_single_task");
    ShuildExample("2_multiple_threads");
    ShuildExample("3_multiple_tasks");
    ShuildExample("4_destroying_tasks");
    ShuildExample("5_destroying_threads");
    ShuildExample("6_yield_delay");

    return 0;
}