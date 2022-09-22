
/* Include Kconfig variables. */
#include <autoconf.h>
#include <sel4test-driver/gen_config.h>

#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>

#include <sel4runtime.h>

#include <allocman/bootstrap.h>
#include <allocman/vka.h>

#include <cpio/cpio.h>

#include <platsupport/local_time_manager.h>

#include <sel4platsupport/timer.h>

#include <sel4debug/register_dump.h>
#include <sel4platsupport/device.h>
#include <sel4platsupport/platsupport.h>
#include <sel4utils/vspace.h>
#include <sel4utils/stack.h>
#include <sel4utils/helpers.h>
#include <sel4utils/process.h>
#include <sel4test/test.h>

#include <simple/simple.h>
#include <simple-default/simple-default.h>

#include <utils/util.h>

#include <vka/object.h>
#include <vka/capops.h>

#include <vspace/vspace.h>
#include "test.h"
#include "timer.h"
#include "../../sel4test-tests/src/helpers.h"

#include <sel4platsupport/io.h>
#include <sel4bench/arch/sel4bench.h>
#include <math.h>

void calculateSD(float data[], float *mean, float *sd,
                 int start, int end)
{
    int i;

    int n = end - start +1;
    float sum = 0.0;
    for (i = 1; i < n; ++i) {
        sum += data[i];
    }
    *mean = sum / n;
    for (i = start; i < end; ++i) {
        *sd += pow(data[i] - *mean, 2);
    }
    *sd = *sd / n;
    *sd = sqrt(*sd);
    return;
}

void test_starting_new_process(driver_env_t env) {
    int error = 0;
    int EP_BADGE = 0x6a;

    simple_t *simple = &env->simple;
    vspace_t *vspace = &env->vspace;
    assert(vspace != NULL);
    assert(vspace->data != NULL);
    vka_t *vka = &env->vka;
    sel4utils_process_t new_process;

    ccnt_t start, end;
    sel4bench_init();
    SEL4BENCH_READ_CCNT(start);

    sel4utils_process_config_t config = process_config_default_simple(simple, "hello", 255);
    error = sel4utils_configure_process_custom(&new_process, vka, vspace, config);
    assert(error == 0);


    /* give the new process's thread a name */
    NAME_THREAD(new_process.thread.tcb.cptr, "dynamic-3: process_2");

    /* create an endpoint */
    vka_object_t ep_object = {0};
    error = vka_alloc_endpoint(vka, &ep_object);
    assert(error == 0);

    /*
     * make a badged endpoint in the new process's cspace.  This copy
     * will be used to send an IPC to the original cap
     */

    cspacepath_t ep_cap_path;
    seL4_CPtr new_ep_cap = 0;
    vka_cspace_make_path(vka, ep_object.cptr, &ep_cap_path);

    
    /* TASK 4: copy the endpont cap and add a badge to the new cap */
    new_ep_cap = sel4utils_mint_cap_to_process(&new_process, ep_cap_path,
                                               seL4_AllRights, EP_BADGE);
    seL4_Word argc = 1;
    char string_args[argc][WORD_STRING_SIZE];
    char* argv[argc];
    sel4utils_create_word_args(string_args, argv, argc, new_ep_cap);

    error = sel4utils_spawn_process_v(&new_process, vka, vspace, argc, (char**) &argv, 1);
    assert(error == 0);

   /*
     * now wait for a message from the new process, then send a reply back
     */

    seL4_Word sender_badge = 0;
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);

    tag = seL4_Recv(ep_cap_path.capPtr, &sender_badge);
   /* make sure it is what we expected */
    assert(sender_badge == EP_BADGE);

    // assert(seL4_MessageInfo_get_length(tag) == 1);


    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    /* get the message stored in the first message register */
    seL4_SetMR(0, start);

    
    seL4_ReplyRecv(ep_cap_path.capPtr, tag, &sender_badge);


    while (1) {
        seL4_ReplyRecv(ep_cap_path.capPtr, tag, &sender_badge);
    }
}



int new_thread(seL4_Word mains_ep, seL4_Word arg1)
{
    ccnt_t creation_end, creation_start;
    ccnt_t ctx_end, ctx_start;
    SEL4BENCH_READ_CCNT(creation_end);

    seL4_SetIPCBuffer((seL4_IPCBuffer*)arg1);
    assert(mains_ep != 0);
    // printf("hello: __sel4_ipc_buffer(%p): %p\n", &__sel4_ipc_buffer, __sel4_ipc_buffer);

    /*
     * send a message to our parent with our start time , and wait for a reply
     */

    /* set the data to send. We send it in the first message register */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);

    tag = seL4_Call(mains_ep, tag);
    creation_start = seL4_GetMR(0);

    printf("test_fun: creationg time %llu\n",
     (creation_end - creation_start)/1000);

    float data[1000];
    printf("test_func_die: Cross AS IPC Time\n");
    int count = 1000;
    int i = 0;
    for(int i = 0; i < count; i++) {

        tag = seL4_MessageInfo_new(0, 0, 0, 1);
        SEL4BENCH_READ_CCNT(ctx_start);
        tag = seL4_Call(mains_ep, tag);
        SEL4BENCH_READ_CCNT(ctx_end);
        data[i] = (ctx_end - ctx_start)/2;
    }
    
    float mean, sd;
    calculateSD(data, &mean, &sd, 1, 99);
    printf("MEAN: %f, SD: %f \n", mean/1000, sd/1000);

    while(1); // As I do not know how to cleanly exit the thread, I just loop forever
}

void test_starting_new_threads(driver_env_t env) 
{
    int EP_BADGE = 0x6a;
    vka_t *vka = &env->vka;
    vka_object_t tcb;
    vspace_t *vspace = &env->vspace;
    assert(vspace != NULL);

    ccnt_t start, end;
    sel4bench_init();
    SEL4BENCH_READ_CCNT(start);

    vka_object_t ep_object = {0};
    int error = vka_alloc_endpoint(&env->vka, &ep_object);
    assert(error == 0);
    
    seL4_CNode root_cnode = simple_get_cnode(&env->simple);
    size_t cnode_size_bits = simple_get_cnode_size_bits(&env->simple);
    error = vka_alloc_tcb(vka, &tcb);
    assert(error == 0);

    seL4_CPtr vspace_root  = vspace->get_root(vspace); // root page table
    assert(vspace_root != 0);
    
    seL4_CPtr ipc_buffer_frame;
    void *ipc_buffer_addr =  vspace_new_ipc_buffer(vspace, &ipc_buffer_frame);
    assert(ipc_buffer_addr != NULL);

    void *stack_top = vspace_new_sized_stack(vspace, 8);
    assert(stack_top != NULL);

    void *tls_base = stack_top;
    stack_top -= 0x100;

    error = seL4_TCB_Configure(tcb.cptr,
                               seL4_CapNull,             // fault endpoint
                               root_cnode,               // root cnode
                               0,                        // root cnode size
                               vspace_root,
                               0,                        // domain
                               (seL4_Word)ipc_buffer_addr,
                               ipc_buffer_frame);
    assert(error == 0);

    error = seL4_TCB_SetPriority(tcb.cptr, seL4_CapInitThreadTCB, 254);
    assert(error == 0);

                                
    error = seL4_TCB_SetTLSBase(tcb.cptr, (seL4_Word)stack_top);
    assert(error == 0);

    stack_top -= 0x100;

     UNUSED seL4_UserContext regs = {0};
    error = seL4_TCB_ReadRegisters(tcb.cptr,
                                       0, 0, sizeof(regs) / sizeof(seL4_Word), &regs);
    assert(error == 0);
    sel4utils_arch_init_local_context((sel4utils_thread_entry_fn )new_thread,
                                      (void *)ep_object.cptr,
                                      (void *)ipc_buffer_addr,
                                      NULL,
                                      stack_top, &regs);
    assert(error == 0);

    error = seL4_TCB_WriteRegisters(tcb.cptr, 0, 0, sizeof(regs)/sizeof(seL4_Word), &regs);
    assert(error == 0);


    // resume the new thread
    error = seL4_TCB_Resume(tcb.cptr);

    /* Wait for the thread to finish */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(ep_object.cptr, NULL);

    /* Send back a funny response */
    seL4_SetMR(0, start);

    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_ReplyRecv(ep_object.cptr, tag, NULL);
    
    while (1) {
        seL4_ReplyRecv(ep_object.cptr, tag, NULL);
    }
}