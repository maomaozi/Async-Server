#pragma once

#include <vector>
#include <functional>
#include "queue_lock_free.h"

typedef struct connection_info
{
	int owner = 0;
	int fd = -1;

	std::string ip;
	__uint32_t port = 0;
	__uint64_t connection_id = 0;

	// same as other struct
	connection_info(int owner, size_t): owner(owner)
	{
		;
	};
} connection_info_t, *pconnection_info_t;


typedef struct data_package
{
	int owner = 0;
	// fly weight
	pconnection_info_t connect = nullptr;

	char * const data = nullptr;
	char * ref_data = nullptr;

	bool by_ref = false;
	//void (*deleter)(void*) = nullptr;
    std::function<void(char *)> deleter;

	int offset = 0;
	__uint32_t data_length = 0;

	enum status_t
	{
		KEEP = 10,
		//STREAM_DONE,
		CLOSE,
	} status = KEEP;

	data_package(int owner, size_t package_size): owner(owner), data(new char[package_size])
	{
		;
	}

	~data_package()
	{
		if (data) delete []data;
	}

} data_package_t, *pdata_package_t;


template <typename T>
class pre_alloc_pool
{
public:
	pre_alloc_pool()
	{
		;
	};

	void init(int owner, int package_num, size_t package_size = 1)
	{

		all_packages.reserve(package_num + 1);
		remain_package_num = package_num;

		for (int i = 0; i < package_num; ++i)
		{
			T *p_package_struct = new T(owner, package_size);

			all_packages.push_back(p_package_struct);
			container.enqueue(p_package_struct);
		}
	};

	~pre_alloc_pool()
	{
		for (auto iter = all_packages.begin(); iter != all_packages.end(); ++iter)
		{
			if (!(*iter))
			{
				// if there is some mistake
				// a nullptr push into vector
				continue;
			}

			// delete struct it self
			delete *iter;
		}
	};


	T *get()
	{
		T *package;

		container.wait_dequeue(package);
		--remain_package_num;

		return package;
	}


	void put(T *package)
	{
		container.enqueue(package);
		++remain_package_num;
	}

	__uint32_t remain()
	{
		return remain_package_num;
	}


private:

	BlockinglockFreeQueue<T*> container;
	std::vector<T*> all_packages;

	__uint32_t remain_package_num = 0;
};
