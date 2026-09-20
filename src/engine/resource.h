#pragma once

#define RESOURCE_MAX_NUM 64

typedef struct Engine Engine;

typedef void(*ResourceReleaseFunc)(Engine*, void*) ;

typedef struct Resource {
    void* ptr;
    ResourceReleaseFunc release;
} Resource;

typedef struct ResourceTracker {
    Resource* resources;
    int       resources_num;
} ResourceTracker;

void engine_resource_track(Engine* self, void* ptr, ResourceReleaseFunc release);

