
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
    ZF_LOGF_IFERR(error, "Failed to allocate new endpoint object.\n");

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
    ZF_LOGF_IFERR(error, "Failed to spawn and start the new thread.\n"
                  "\tVerify: the new thread is being executed in the root thread's VSpace.\n"
                  "\tIn this case, the CSpaces are different, but the VSpaces are the same.\n"
                  "\tDouble check your vspace_t argument.\n");

    /*
     * now wait for a message from the new process, then send a reply back
     */

    seL4_Word sender_badge = 0;
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    seL4_Word msg;

    tag = seL4_Recv(ep_cap_path.capPtr, &sender_badge);
   /* make sure it is what we expected */
    ZF_LOGF_IF(sender_badge != EP_BADGE,
               "The badge we received from the new thread didn't match our expectation.\n");

    ZF_LOGF_IF(seL4_MessageInfo_get_length(tag) != 1,
               "Response data from the new process was not the length expected.\n"
               "\tHow many registers did you set with seL4_SetMR within the new process?\n");


    /* get the message stored in the first message register */
    end = seL4_GetMR(0);
    printf("root-task: \tStart: %010ld\n\t, End: %ld\n\t, Diff: %ld\n",
           start, end, end - start);

    /* modify the message */
    seL4_SetMR(0, ~msg);

    
    /* TASK 7: send the modified message back */
    /* hint 1: seL4_ReplyRecv()
     * seL4_MessageInfo_t seL4_ReplyRecv(seL4_CPtr dest, seL4_MessageInfo_t msgInfo, seL4_Word *sender)
     * @param dest The capability to be invoked.
     * @param msgInfo The messageinfo structure for the IPC.  This specifies information about the message to send (such as the number of message registers to send) as the Reply part.
     * @param sender The badge of the endpoint capability that was invoked by the sender is written to this address. This is a result of the Wait part.
     * @return A seL4_MessageInfo_t structure.  This is a result of the Wait part.
     *
     * hint 2: seL4_MessageInfo_t is generated during build.
     * hint 3: use the badged endpoint cap that you used for Call
     */
    
    seL4_ReplyRecv(ep_cap_path.capPtr, tag, &sender_badge);
}



int new_thread(seL4_Word mains_ep, seL4_Word arg1){

    assert(mains_ep != 0);
    ccnt_t end;
    SEL4BENCH_READ_CCNT(end);
    printf("hello: __sel4_ipc_buffer(%p): %p\n", &__sel4_ipc_buffer, __sel4_ipc_buffer);

    /*
     * send a message to our parent, and wait for a reply
     */

    /* set the data to send. We send it in the first message register */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, end);

    tag = seL4_Call(mains_ep, tag);

    seL4_Word msg = seL4_GetMR(0);

    printf("hello: got a reply: %lu\n", msg);

    while(1); // As I do not know how to cleanly exit the thread, I just loop forever
}

void test_starting_new_threads(driver_env_t env) {


    int error = 0;
    int EP_BADGE = 0x6a;

    simple_t *simple = &env->simple;
    vspace_t *vspace = &env->vspace;
    assert(vspace != NULL);
    assert(vspace->data != NULL);
    vka_t *vka = &env->vka;
    seL4_CNode root_cnode = simple_get_cnode(&env->simple);
    size_t cnode_size_bits = simple_get_cnode_size_bits(&env->simple);

    sel4utils_process_t new_process;

    ccnt_t start, end;
    sel4bench_init();
    SEL4BENCH_READ_CCNT(start);

    /* Setup the Thread */
    helper_thread_t thread;


    error = vka_alloc_endpoint(&env->vka, &thread.local_endpoint);
    assert(error == 0);

    size_t stack_pages = BYTES_TO_4K_PAGES(CONFIG_SEL4UTILS_STACK_SIZE);
    thread.is_process = false;
    thread.fault_endpoint = env->endpoint;
    seL4_Word data = api_make_guard_skip_word(seL4_WordBits - cnode_size_bits);
    sel4utils_thread_config_t config = thread_config_default(&env->simple, root_cnode, data, env->endpoint, 244);
    config = thread_config_stack_size(config, stack_pages);
    error = sel4utils_configure_thread_config(&env->vka, &env->vspace, &env->vspace,
                                              config, &thread.thread);
    assert(error == 0);
    //start_helper(env, &thread, new_thread, 0, 0, 0, 0);

    // sel4utils_create_word_args(thread.args_strings, thread.args, HELPER_THREAD_TOTAL_ARGS,
//                                0xdead, 0xbeef);

    // from sel4utils_start_thread
    seL4_UserContext context = {0};
    size_t context_size = sizeof(seL4_UserContext) / sizeof(seL4_Word);

    size_t tls_size = sel4runtime_get_tls_size();
    sel4utils_thread_t th = thread.thread;
    /* make sure we're not going to use too much of the stack */
    if (tls_size > th.stack_size * PAGE_SIZE_4K / 8) {
        ZF_LOGE("TLS would use more than 1/8th of the application stack %zu/%zu", tls_size, th.stack_size);
        return;
    }
    uintptr_t tls_base = (uintptr_t)th.initial_stack_pointer - tls_size;
    uintptr_t tp = (uintptr_t)sel4runtime_write_tls_image((void *)tls_base);
    seL4_IPCBuffer *ipc_buffer_addr = (void *)th.ipc_buffer_addr;
    sel4runtime_set_tls_variable(tp, __sel4_ipc_buffer, ipc_buffer_addr);

    uintptr_t aligned_stack_pointer = ALIGN_DOWN(tls_base, STACK_CALL_ALIGNMENT);

    // context.x0 = thread.local_endpoint.cptr;
    // context.x1 = 0xbeef;

    // error = sel4utils_arch_init_context(new_thread,
    //                                     (void *)aligned_stack_pointer, /* stack_top */
    //                                     &context);
    error = sel4utils_arch_init_context_with_args((sel4utils_thread_entry_fn)new_thread,
                                                  (void*)thread.local_endpoint.cptr,
                                                  (void*)0xbeef,
                                                  NULL,
                                                  false,
                                                  (void *)aligned_stack_pointer, /* stack_top */
                                                  &context,
                                                  NULL,
                                                  NULL,
                                                  NULL);
    assert(error == 0);

    error = seL4_TCB_WriteRegisters(th.tcb.cptr, false, 0, context_size, &context);
    assert(error == 0);

    error = seL4_TCB_SetTLSBase(th.tcb.cptr, tp);
    assert(error == 0);

    error = seL4_TCB_Resume(th.tcb.cptr);
    assert(error == 0);

    /* Wait for the thread to finish */

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    seL4_Word msg;

    tag = seL4_Recv(thread.local_endpoint.cptr, NULL);
    // ZF_LOGF_IF(seL4_MessageInfo_get_length(tag) != 1,
    //            "Response data from the new process was not the length expected.\n"
    //            "\tHow many registers did you set with seL4_SetMR within the new process?\n");


    /* get the message stored in the first message register */
    end = seL4_GetMR(0);
    printf("root-task: \tStart: %010ld\n\t, End: %ld\n\t, Diff: %ld\n",
           start, end, end - start);

    printf("root_task: __sel4_ipc_buffer(%p): %p\n", &__sel4_ipc_buffer, __sel4_ipc_buffer);
    /* modify the message */
    seL4_SetMR(0, ~msg);

    seL4_ReplyRecv(thread.local_endpoint.cptr, tag, NULL);
}