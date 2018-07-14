#pragma once

#include <vector>
#include <stack>


struct connection_info_t
{
	int fd = -1;

	std::string ip;
	__uint32_t port = 0;
	__uint64_t connection_id = 0;
};

typedef connection_info_t *pconnection_info_t;


class connection_info_pool
{
public:
	connection_info_pool()
	{
		;
	}

	void init(size_t info_num)
	{
		all_info.reserve(info_num + 1);
		remain_info_num = info_num;

		for (int i = 0; i < info_num; ++i)
		{
			pconnection_info_t p_info_struct = new connection_info_t();

			all_info.push_back(p_info_struct);
			container.push(p_info_struct);
		}
	};

	~connection_info_pool()
	{
		for (auto iter = all_info.begin(); iter != all_info.end(); ++iter)
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


	pconnection_info_t get()
	{
		pconnection_info_t info = container.top();

		container.pop();
		--remain_info_num;

		return info;
	}


	void put(pconnection_info_t info)
	{
		container.push(info);
		++remain_info_num;
	}

	__uint32_t remain()
	{
		return remain_info_num;
	}


private:

	std::stack<pconnection_info_t> container;
	std::vector<pconnection_info_t> all_info;

	__uint32_t remain_info_num = 0;
};



struct data_package_t
{
	// fly weight
	pconnection_info_t connect = nullptr;

	char * const data = nullptr;
	int offset = 0;
	__uint32_t data_length = 0;

	enum status_t
	{
		KEEP = 10,
		//STREAM_DONE,
		CLOSE,
	} status = KEEP;

	data_package_t(char * init_data_ptr): data(init_data_ptr)
	{
		;
	}
};


typedef data_package_t *pdata_package_t;

class data_package_pool
{
public:
	data_package_pool()
	{
		;
	};

	void init(int package_num, size_t package_size)
	{

		all_packages.reserve(package_num + 1);
		remain_package_num = package_num;

		for (int i = 0; i < package_num; ++i)
		{
			// alocate space for each struct's data member
			char *data = new char[package_size];

			pdata_package_t p_package_struct = new data_package_t(data);

			all_packages.push_back(p_package_struct);
			container.push(p_package_struct);
		}
	};

	~data_package_pool()
	{
		for (auto iter = all_packages.begin(); iter != all_packages.end(); ++iter)
		{
			if (!(*iter))
			{
				// if there is some mistake
				// a nullptr push into vector
				continue;
			}

			// prevent if some accident happend
			// data is not allocate correctly
			if ((*iter)->data)
			{
				// delete space allocate in space
				delete (*iter)->data;
			}

			// delete struct it self
			delete *iter;
		}
	};


	pdata_package_t get()
	{
		pdata_package_t package = container.top();

		container.pop();
		--remain_package_num;

		return package;
	}


	void put(pdata_package_t package)
	{
		container.push(package);
		++remain_package_num;
	}

	__uint32_t remain()
	{
		return remain_package_num;
	}


private:

	std::stack<pdata_package_t> container;
	std::vector<pdata_package_t> all_packages;

	__uint32_t remain_package_num = 0;
};
