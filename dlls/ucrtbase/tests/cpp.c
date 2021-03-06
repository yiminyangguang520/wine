/*
 * Copyright 2016 Daniel Lehman (Esri)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdio.h>

#include <windef.h>
#include <winbase.h>
#include "wine/test.h"

typedef unsigned char MSVCRT_bool;

typedef struct {
    const char  *what;
    MSVCRT_bool  dofree;
} __std_exception_data;

typedef struct
{
    char *name;
    char mangled[32];
} type_info140;

typedef struct _type_info_list
{
    SLIST_ENTRY entry;
    char name[1];
} type_info_list;

static void* (CDECL *p_malloc)(size_t);
static void (CDECL *p___std_exception_copy)(const __std_exception_data*, __std_exception_data*);
static void (CDECL *p___std_exception_destroy)(__std_exception_data*);
static int (CDECL *p___std_type_info_compare)(const type_info140*, const type_info140*);
static const char* (CDECL *p___std_type_info_name)(type_info140*, SLIST_HEADER*);
static void (CDECL *p___std_type_info_destroy_list)(SLIST_HEADER*);


static BOOL init(void)
{
    HMODULE module;

    module = LoadLibraryA("ucrtbase.dll");
    if (!module)
    {
        win_skip("ucrtbase.dll not installed\n");
        return FALSE;
    }

    p_malloc = (void*)GetProcAddress(module, "malloc");
    p___std_exception_copy = (void*)GetProcAddress(module, "__std_exception_copy");
    p___std_exception_destroy = (void*)GetProcAddress(module, "__std_exception_destroy");
    p___std_type_info_compare = (void*)GetProcAddress(module, "__std_type_info_compare");
    p___std_type_info_name = (void*)GetProcAddress(module, "__std_type_info_name");
    p___std_type_info_destroy_list = (void*)GetProcAddress(module, "__std_type_info_destroy_list");
    return TRUE;
}

static void test___std_exception(void)
{
    __std_exception_data src;
    __std_exception_data dst;

    if (0) /* crash on Windows */
    {
        p___std_exception_copy(NULL, &src);
        p___std_exception_copy(&dst, NULL);

        src.what   = "invalid free";
        src.dofree = 1;
        p___std_exception_destroy(&src);
        p___std_exception_destroy(NULL);
    }

    src.what   = "what";
    src.dofree = 0;
    p___std_exception_copy(&src, &dst);
    ok(dst.what == src.what, "expected what to be same, got src %p dst %p\n", src.what, dst.what);
    ok(!dst.dofree, "expected 0, got %d\n", dst.dofree);

    src.dofree = 0x42;
    p___std_exception_copy(&src, &dst);
    ok(dst.what != src.what, "expected what to be different, got src %p dst %p\n", src.what, dst.what);
    ok(dst.dofree == 1, "expected 1, got %d\n", dst.dofree);

    p___std_exception_destroy(&dst);
    ok(!dst.what, "expected NULL, got %p\n", dst.what);
    ok(!dst.dofree, "expected 0, got %d\n", dst.dofree);

    src.what = NULL;
    src.dofree = 0;
    p___std_exception_copy(&src, &dst);
    ok(!dst.what, "dst.what != NULL\n");
    ok(!dst.dofree, "dst.dofree != FALSE\n");

    src.what = NULL;
    src.dofree = 1;
    p___std_exception_copy(&src, &dst);
    ok(!dst.what, "dst.what != NULL\n");
    ok(!dst.dofree, "dst.dofree != FALSE\n");
}

static void test___std_type_info(void)
{
    type_info140 ti1 = { NULL, ".?AVa@@" };
    type_info140 ti2 = { NULL, ".?AVb@@" };
    type_info140 ti3 = ti1;
    SLIST_HEADER header;
    type_info_list *elem;
    const char *ret;
    int eq;


    InitializeSListHead(&header);
    p___std_type_info_destroy_list(&header);

    elem = p_malloc(sizeof(*elem));
    memset(elem, 0, sizeof(*elem));
    InterlockedPushEntrySList(&header, &elem->entry);
    p___std_type_info_destroy_list(&header);
    ok(!InterlockedPopEntrySList(&header), "list is not empty\n");

    ret = p___std_type_info_name(&ti1, &header);
    ok(!strcmp(ret, "class a"), "__std_type_info_name(&ti1) = %s\n", ret);
    ok(ti1.name == ret, "ti1.name = %p, ret = %p\n", ti1.name, ret);

    p___std_type_info_destroy_list(&header);
    ok(!InterlockedPopEntrySList(&header), "list is not empty\n");
    ok(ti1.name == ret, "ti1.name = %p, ret = %p\n", ti1.name, ret);
    ti1.name = NULL;

    eq = p___std_type_info_compare(&ti1, &ti1);
    ok(eq == 0, "__std_type_info_compare(&ti1, &ti1) = %d\n", eq);

    eq = p___std_type_info_compare(&ti1, &ti2);
    ok(eq == -1, "__std_type_info_compare(&ti1, &ti2) = %d\n", eq);

    eq = p___std_type_info_compare(&ti1, &ti3);
    ok(eq == 0, "__std_type_info_compare(&ti1, &ti3) = %d\n", eq);
}

START_TEST(cpp)
{
    if (!init()) return;
    test___std_exception();
    test___std_type_info();
}
