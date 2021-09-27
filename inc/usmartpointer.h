/*
 * Copyright 2021 Alexey Vasilenko
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef USMARTPOINTER_H_
#define USMARTPOINTER_H_

#include "dalloc.h"

#define USMARTPOINTER_VERSION			"1.1.1"

#define SMART_PTR_DEBUG					printf

template <typename T>
class SmartPointer {
private:
	heap_t *alloc_mem_ptr;
	T* ptr_val;
	bool replace_flag;
    bool *replace_flag_ptr;

public:
	SmartPointer();
#ifdef USE_SINGLE_HEAP_MEMORY
    SmartPointer(T* &ptr);
#else
    SmartPointer(heap_t *_alloc_mem_ptr, T* &ptr);
#endif

    SmartPointer(const SmartPointer &sm_ptr_obj);
	~SmartPointer();

	void assignAllocMemPointer(heap_t* ptr);
	bool assignPtr(T* &ptr);
	heap_t* getMemPtr() const;

	T* get();
    T& operator*();
    T& operator[](uint32_t i);
    SmartPointer<T>& operator = (const SmartPointer &sm_ptr_obj);
    SmartPointer<T>& operator = (T* &ptr);

	bool allocate(uint32_t elements_num);
    bool free();
};

#ifdef USE_SINGLE_HEAP_MEMORY
template <typename T>
SmartPointer<T>::SmartPointer(){
    alloc_mem_ptr = &default_heap;
    ptr_val = NULL;
    replace_flag_ptr = &replace_flag;
    replace_flag = false;
}

template <typename T>
SmartPointer<T>::SmartPointer(T* &ptr){
    assignAllocMemPointer(&default_heap);
    if(assignPtr(ptr) != true){
        SMART_PTR_DEBUG("SmartPointer<T>:: can't assign pointer\n");
    }
    replace_flag_ptr = &replace_flag;
    replace_flag = false;
}
#else
template <typename T>
SmartPointer<T>::SmartPointer(){
    alloc_mem_ptr = NULL;
    ptr_val = NULL;
    replace_flag_ptr = &replace_flag;
    replace_flag = false;
}

template <typename T>
SmartPointer<T>::SmartPointer(heap_t *_alloc_mem_ptr, T* &ptr){
    assignAllocMemPointer(_alloc_mem_ptr);
    if(assignPtr(ptr) != true){
        SMART_PTR_DEBUG("SmartPointer<T>:: can't assign pointer\n");
    }
    replace_flag_ptr = &replace_flag;
    replace_flag = false;
}
#endif


template <typename T>
SmartPointer<T>::SmartPointer(const SmartPointer &sm_ptr_obj){
	assignAllocMemPointer(sm_ptr_obj.getMemPtr());

    if(validate_ptr(this->alloc_mem_ptr, (void**)&(sm_ptr_obj.ptr_val), USING_PTR_ADDRESS, NULL)){
        replace_pointers(this->alloc_mem_ptr, (void**)&(sm_ptr_obj.ptr_val), (void**)&(this->ptr_val));
        *sm_ptr_obj.replace_flag_ptr = true;
    }
    else {
        SMART_PTR_DEBUG("SmartPointer<T>:: can't replace pointers\n");
    }
}

template <typename T>
SmartPointer<T>& SmartPointer<T>::operator = (const SmartPointer &sm_ptr_obj){
	if(&sm_ptr_obj != this){
        if(validate_ptr(this->alloc_mem_ptr, (void**)&(sm_ptr_obj.ptr_val), USING_PTR_ADDRESS, NULL)){
            replace_pointers(this->alloc_mem_ptr, (void**)&(sm_ptr_obj.ptr_val), (void**)&(this->ptr_val));
            *sm_ptr_obj.replace_flag_ptr = true;
        }
        else {
            SMART_PTR_DEBUG("SmartPointer<T>:: can't replace pointers\n");
        }
	}
	return *this;
};

template <typename T>
SmartPointer<T>& SmartPointer<T>::operator = (T* &ptr){
    if(&ptr != this){
        if(validate_ptr(this->alloc_mem_ptr, (void**)&ptr, USING_PTR_ADDRESS, NULL)){
            replace_pointers(this->alloc_mem_ptr, (void**)&ptr, (void**)&(this->ptr_val));
        }
        else {
            SMART_PTR_DEBUG("SmartPointer<T>:: can't replace pointers\n");
        }
    }
    return *this;
};

template <typename T>
SmartPointer<T>::~SmartPointer(){
    if((alloc_mem_ptr != NULL)&&(ptr_val != NULL)&&(replace_flag == false)){
        dfree(alloc_mem_ptr, (void**)&ptr_val, USING_PTR_ADDRESS);
    }
}

template <typename T>
void SmartPointer<T>::assignAllocMemPointer(heap_t* ptr){
	alloc_mem_ptr = ptr;
}

template <typename T>
bool SmartPointer<T>::assignPtr(T* &ptr){
    if(alloc_mem_ptr == NULL){
		return false;
	}
    if(validate_ptr(this->alloc_mem_ptr, (void**)&ptr, USING_PTR_ADDRESS, NULL)){
        replace_pointers(this->alloc_mem_ptr, (void**)&ptr, (void**)&ptr_val);
        return true;
    }
    return false;
}

template <typename T>
T* SmartPointer<T>::get(){
	return ptr_val;
}

template <typename T>
T& SmartPointer<T>::operator*(){
	return *ptr_val;
}

template <typename T>
T& SmartPointer<T>::operator[](uint32_t i){
	return ptr_val[i];
}

template <typename T>
heap_t* SmartPointer<T>::getMemPtr() const{
	return this->alloc_mem_ptr;
}

template <typename T>
bool SmartPointer<T>::allocate(uint32_t elements_num){
    if(alloc_mem_ptr == NULL){
		SMART_PTR_DEBUG("SmartPointer<T>:: can't allocate memory, allocate mem pointer is uninitialized\n");
		return false;
	}
    if(ptr_val != NULL){
		SMART_PTR_DEBUG("SmartPointer<T>:: can't allocate memory, ptr value is already in use\n");
		return false;
	}

    dalloc(alloc_mem_ptr, elements_num*sizeof(T), (void**)&ptr_val);
    if(ptr_val == NULL){
		SMART_PTR_DEBUG("SmartPointer<T>:: can't allocate memory, dalloc error\n");
		return false;
	}
	return true;
}

template <typename T>
bool SmartPointer<T>::free(){
    if((alloc_mem_ptr != NULL)&&(ptr_val != NULL)){
        dfree(alloc_mem_ptr, (void**)&ptr_val, USING_PTR_ADDRESS);
        return true;
    }
    return false;
}

#endif /* USMARTPOINTER_H_ */
