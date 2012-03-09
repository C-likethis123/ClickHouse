#include <Poco/Net/HTTPServerRequest.h>

#include <Yandex/ApplicationServerExt.h>

#include <DB/Functions/FunctionsLibrary.h>
#include <DB/Interpreters/loadMetadata.h>
#include <DB/Storages/StorageSystemNumbers.h>
#include <DB/Storages/StorageSystemOne.h>

#include "Server.h"
#include "HTTPHandler.h"
#include "TCPHandler.h"


namespace DB
{

/// Отвечает "Ok.\n", если получен любой GET запрос. Используется для проверки живости.
class PingRequestHandler : public Poco::Net::HTTPRequestHandler
{
public:
	PingRequestHandler()
	{
	    LOG_TRACE((&Logger::get("PingRequestHandler")), "Ping request.");
	}

	void handleRequest(Poco::Net::HTTPServerRequest & request, Poco::Net::HTTPServerResponse & response)
	{
		response.send() << "Ok." << std::endl;
	}
};


Poco::Net::HTTPRequestHandler * HTTPRequestHandlerFactory::createRequestHandler(
	const Poco::Net::HTTPServerRequest & request)
{
	LOG_TRACE(log, "HTTP Request. "
		<< "Method: " << request.getMethod()
		<< ", Address: " << request.clientAddress().toString()
		<< ", User-Agent: " << (request.has("User-Agent") ? request.get("User-Agent") : "none"));

	if (request.getURI().find('?') != std::string::npos)
		return new HTTPHandler(server);
	else if (request.getMethod() == Poco::Net::HTTPRequest::HTTP_GET)
		return new PingRequestHandler();
	else
		return 0;
}


Poco::Net::TCPServerConnection * TCPConnectionFactory::createConnection(const Poco::Net::StreamSocket & socket)
{
	LOG_TRACE(log, "TCP Request. " << "Address: " << socket.address().toString());

	return new TCPHandler(server, socket);
}


int Server::main(const std::vector<std::string> & args)
{
	/// Заранее инициализируем DateLUT, чтобы первая инициализация потом не влияла на измеряемую скорость выполнения.
	Yandex::DateLUTSingleton::instance();

	/** Контекст содержит всё, что влияет на обработку запроса:
	  *  настройки, набор функций, типов данных, агрегатных функций, баз данных...
	  */
	global_context.path = config.getString("path");
	global_context.functions = FunctionsLibrary::get();
	global_context.aggregate_function_factory	= new AggregateFunctionFactory;
	global_context.data_type_factory			= new DataTypeFactory;
	global_context.storage_factory				= new StorageFactory;

	loadMetadata(global_context);

	(*global_context.databases)["system"]["one"] 		= new StorageSystemOne("one");
	(*global_context.databases)["system"]["numbers"] 	= new StorageSystemNumbers("numbers");
		
	global_context.current_database = config.getString("default_database", "default");

	global_context.settings.asynchronous 	= config.getBool("asynchronous", 	global_context.settings.asynchronous);
	global_context.settings.max_block_size 	= config.getInt("max_block_size", 	global_context.settings.max_block_size);
	global_context.settings.max_query_size 	= config.getInt("max_query_size", 	global_context.settings.max_query_size);
	global_context.settings.max_threads 	= config.getInt("max_threads", 		global_context.settings.max_threads);
	
	Poco::Net::ServerSocket http_socket(Poco::Net::SocketAddress("[::]:" + config.getString("http_port")));
	Poco::Net::ServerSocket tcp_socket(Poco::Net::SocketAddress("[::]:" + config.getString("tcp_port")));

	Poco::ThreadPool server_pool(2, config.getInt("max_threads", 128));

	Poco::Net::HTTPServer http_server(
		new HTTPRequestHandlerFactory(*this),
		server_pool,
		http_socket,
		new Poco::Net::HTTPServerParams);

	Poco::Net::TCPServer tcp_server(
		new TCPConnectionFactory(*this),
		server_pool,
		tcp_socket,
		new Poco::Net::TCPServerParams);

	http_server.start();
	tcp_server.start();

	waitForTerminationRequest();

	http_server.stop();
	tcp_server.stop();
	
	return Application::EXIT_OK;
}

}


YANDEX_APP_SERVER_MAIN(DB::Server);
