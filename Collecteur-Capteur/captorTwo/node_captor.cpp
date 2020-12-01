/*
 *                  CAPTOR
 */
/* Cette constante permet d’utiliser les versions "thread safe" des */
/* fonction de la lib C elle est OBLIGATOIRE */
#define _REENTRANT

#define DEBUG

#include <algorithm>
#include <cstring>
#include <dirent.h> 
#include <experimental/filesystem>
#include <iostream>
#include <fstream>
#include <mutex>
#include <pthread.h>
#include <signal.h>
#include <string>
#include <vector>
#include <unistd.h>

// Custom libraries
#include "../lib/net_aux.h"

// Server Configuration
#define CHECK_INTERVAL 2000000

// Buffer Config
#define BUFFERMAX 100

// Event names
#define DATA_AVAILABLE "dataAvailable"
#define EVT_DATA_AVAILABLE_ACK "dataAvailableAck"
#define EVT_GET_NODES "getNodes"
#define EVT_GET_NODES_END "endNodeList"
#define EVT_GET_DATA "getDataFile"
#define EVT_GET_DATA_END "getDataFileEnd"
#define SEEK_ECO_CONFIG "seekEcoConfig"
#define SEEK_MIN_CONFIG "seekMinConfig"
#define SEEK_FULL_CONFIG "seekFullConfig"
#define SEEK_ECO_CONFIG_ACK "seekEcoConfigAck"
#define SEEK_MIN_CONFIG_ACK "seekMinConfigAck"
#define SEEK_FULL_CONFIG_ACK "seekFullConfigAck"
#define SEND_ECO_CONFIG "sendEcoConfig"
#define SEND_MIN_CONFIG "sendMinConfig"
#define SEND_FULL_CONFIG "sendFullConfig"


typedef struct{
	int sock;
	char ip_addr[15];
} server_params;

int srv_sock;
std::string ip_addr_df("127.0.1.2"); //adresse par defaut
int port = 8002; // Port par defaut
int sink_port = 8000;

std::vector<std::string> nodeList;
std::vector<std::string> dataList;
std::vector<std::string> dataLost;

std::vector<std::string> receivedConfig;
std::vector<std::string> nodesWithData;

std::string dataDirectory = "data";
std::string dataConfig = "config";

int numeroData = 1;
int max_data_size = 8;

int counter_generated_files = 0;
int counter_lost_files = 0;
int counter_sent_files = 0;

double average_lost_files = 0;
double average_lost_files_binding = 0;

int battery_level = 700;

bool test_battery_600 = false;
bool test_battery_300 = false;
bool test_battery_50 = false;

bool battery_low = false;
bool stop = false;


std::mutex nodeListMutex, dataListMutex, nodesWithDataMutex, dataLostMutex, receivedConfigMutex;

/* A server instance answering client commands */
void* serverFunc(void *s) {

	server_params* params = (server_params*) s;
	int socket = params->sock;
	std::string ip_client(params->ip_addr);
	
	std::cout << "[CAPTOR SERVER] Recieving message from ip : " << ip_client.c_str() << std::endl;
	
	//int socket = *((int*)(&s));
	char buf[BUFFERMAX];

	// Reading command
	sock_receive(socket, buf, BUFFERMAX);

	std::cout << "[CAPTOR SERVER] Receiving message: " << buf << std::endl;

	if (message_is(buf, EVT_GET_DATA)) {
		dataListMutex.lock();
		for (std::string str : dataList) {
		    if (battery_level >= 20) {
		        battery_level -= 20;
                counter_sent_files ++;
                sock_send(socket, str.c_str());
                std::cout << "[CAPTOR BATTERY] Transmission data -20  : " << battery_level << std::endl;
                std::cout << "[CAPTOR INFORMATION] Number of files transmitted  : " << counter_sent_files << std::endl;
		    } else {
		        battery_low = true;
		        std::cout << "[CAPTOR BATTERY] BATTERY SO LOW" << std::endl;
		    }
		}
		// Delete existed data
		if (battery_low == false) {
		    sock_send(socket, EVT_GET_DATA_END); // notify the end of sent for client
            for (std::string dataname : dataList) {
            	std::string filename;
            	filename = dataDirectory + "/" + dataname;
            	remove(filename.c_str());
            }
            dataList.clear();
            stop = false;
            dataListMutex.unlock();
		}
	}


	if(message_is(buf, SEND_ECO_CONFIG)) {
	    if (battery_level >= 30) {
	        /* Lecture du message du client */
            sock_receive(socket, buf, BUFFERMAX);

            receivedConfigMutex.lock();
            std::string newConfig(buf);

            FILE* datafile;
            std::string filename(dataConfig + "/" + newConfig);
            datafile = fopen(filename.c_str(), "w");
            fclose(datafile);
            receivedConfig.push_back(newConfig);
            receivedConfigMutex.unlock();

            battery_level -= 30;
            std::cout << "[CAPTOR MIN_CONFIG] File reception -30 : " << battery_level << std::endl;

	    } else {
	        battery_low = true;
	    }
	}

	if(message_is(buf, SEND_ECO_CONFIG)) {
    	if (battery_level >= 30) {
    	    /* Lecture du message du client */
            sock_receive(socket, buf, BUFFERMAX);

            receivedConfigMutex.lock();
            std::string newConfig(buf);

            FILE* datafile;
            std::string filename(dataConfig + "/" + newConfig);
            datafile = fopen(filename.c_str(), "w");
            fclose(datafile);
            receivedConfig.push_back(newConfig);
            receivedConfigMutex.unlock();

            battery_level -= 30;
            std::cout << "[CAPTOR MIN_CONFIG] File reception -30 : " << battery_level << std::endl;

    	} else {
    	    battery_low = true;
    	}
    }

	// Closing connection
	close_connection(socket);

	pthread_exit(NULL);
}

/* Le thread d'ecoute */
void* listenServer(void *n) {
	/* création de la socket de communication */
	srv_sock = create_socket();

	/* initialisation de la structure representant l'adresse */
	start_server(srv_sock, ip_addr_df.c_str(), port);

	while(1) {
		//server_params* params;
		server_params* params = (server_params*)malloc(sizeof(server_params));
		char buf[BUFFERMAX];
		
		/* Attendre les requêtes de connexion */
		int sock_effective = wait_connection_adr(srv_sock, buf);
		
		//memset(params,0,sizeof(server_params));
		params->sock = sock_effective;
		strncpy(params->ip_addr, buf, strlen(buf));

		pthread_t thrd;
		if (pthread_create(&thrd, NULL, serverFunc, (void *) params) != 0) {
			std::cerr << "[CAPTOR LISTENING] Error while creating a new thread" << std::endl;
			pthread_exit(NULL);
		}
	}
}

/* Mise a jour de la liste des nodes */
void* notifyDataAvailable(void *i) {
	char* ip_serveur = *((char**)(&i));
	char buf[BUFFERMAX];  /* buffer pour les données reçues*/

	/* Création de la socket de communication */
	int clt_sock = create_socket();

	/* demande d'une connexion au serveur */
	open_connection(clt_sock, ip_serveur, sink_port);
	
	std::string delimiter(".");
	std::string message = DATA_AVAILABLE + delimiter + ip_addr_df;
	/* envoi du message au le serveur */
	sock_send(clt_sock, message.c_str());
	battery_level -= 10;
	std::cout << "[CAPTOR BATTERY] Notification Data Available -10 : " << battery_level << std::endl;

	/* Lecture du message du client */
	while(true) {
		sock_receive(clt_sock, buf, BUFFERMAX);
		if (message_is(buf, EVT_DATA_AVAILABLE_ACK)) { break; }
	}
	/* fermeture de la socket */
	close_connection(clt_sock);
}


void* seek_eco_config(void *i) {

	char* ip_serveur = *((char**)(&i));
	char buf[BUFFERMAX];  /* buffer pour les données reçues*/

	/* Création de la socket de communication */
	int clt_sock = create_socket();

	/* demande d'une connexion au serveur */
	open_connection(clt_sock, ip_serveur, sink_port);
	
	std::string delimiter(".");
	std::string message = SEEK_ECO_CONFIG + delimiter + ip_addr_df;
	/* envoi du message au le serveur */
	sock_send(clt_sock, message.c_str());
	
	//Lecture du message du Server(reponse): 
	while(true) {
		sock_receive(clt_sock, buf, BUFFERMAX);
		if (message_is(buf, SEEK_ECO_CONFIG_ACK)) { break; }
	}

	/* fermeture de la socket */
	close_connection(clt_sock);
}


void* seek_min_config(void *i) {
	char* ip_serveur = *((char**)(&i));
	char buf[BUFFERMAX];  /* buffer pour les données reçues*/

	/* Création de la socket de communication */
	int clt_sock = create_socket();

	/* demande d'une connexion au serveur */
	open_connection(clt_sock, ip_serveur, sink_port);
	
	std::string delimiter(".");
	std::string message = SEEK_MIN_CONFIG + delimiter + ip_addr_df;
	/* envoi du message au le serveur */
	sock_send(clt_sock, message.c_str());
	
	//Lecture du message du Server(reponse): 
	while(true) {
		sock_receive(clt_sock, buf, BUFFERMAX);
		if (message_is(buf, SEEK_MIN_CONFIG_ACK)) { break; }
	}
	
	/* fermeture de la socket */
	close_connection(clt_sock);
}

/* Client principal */
void* clientFunc(void *n) {
		
	while(1) {
		//std::cout << "[CLIENT] Sensing data ..." << std::endl;
		//créer la prochaine data
		FILE* datafile;
		std::string num = std::to_string(numeroData);
		std::string filename;

		if(stop == false) {
		    if (battery_level >= 50) {
		        battery_level -= 50;
                std::cout << "[CAPTOR CREAT] File creation -50 : " << battery_level << std::endl;
                filename = dataDirectory + "/" + ip_addr_df + "_data_" + num;
                datafile = fopen(filename.c_str(), "w");
                fclose(datafile);
                dataListMutex.lock();
                std::cout << "[CAPTOR ADD] Adding data to dataList" << filename.c_str() << std::endl;
                dataList.push_back(ip_addr_df + "_data_" + num);
                counter_generated_files ++;
                std::cout << "[CAPTOR INFO] Generated files : " << counter_generated_files << std::endl;
                numeroData++;
                dataListMutex.unlock();
		    } else {
		        battery_low = true;
		    }
		}
		else if(stop == true && battery_low == false) {

		    filename = dataDirectory + "/" + ip_addr_df + "_data_" + num + "_lost";
		    dataLostMutex.lock();
		    std::cout << "[CAPTOR ADD] Adding data lost " << filename.c_str() << " to dataLost " << std::endl;
		    dataLost.push_back(ip_addr_df + "_data_" + num);


		    counter_lost_files ++;
		    std::cout << "[CAPTOR INFO] Number of lost files " << counter_lost_files << std::endl;

            numeroData++;
            counter_generated_files++;

            dataLostMutex.unlock();
		}

		std::cout << "[CAPTOR LIST] List of current data in dataList " << std::endl;
		for (std::string dataname : dataList) {
		    std::cout << "[CAPTOR DATA] " << dataname << std::endl;
		}

		std::cout << "[CAPTOR LIST] List of current data in dataListPerdu " << std::endl;
		for (std::string dataname : dataLost) {
		    std::cout << "[CAPTOR DATA LOST] " << dataname << std::endl;
		}
		
		if(dataList.size() >= max_data_size && stop == false)
		{
		    stop = true;
			pthread_t traitement;
			if(pthread_create(&traitement, NULL, notifyDataAvailable, (void *) nodeList.at(0).c_str()) != 0) {
			    std::cerr << "[CAPTOR CLIENT] Error while creating a new thread: client sensor data update" << std::endl;
			    pthread_exit(NULL);
		    }
		}
		
		//Check battery 600 and seeking eco_config
		if(battery_level <= 600 && test_battery_600 == false)
		{
		    test_battery_600 = true;
			pthread_t traitement;
			if(pthread_create(&traitement, NULL, seek_eco_config, (void *) nodeList.at(0).c_str()) != 0) {
			    std::cerr << "[CAPTOR CLIENT] Error while creating a new thread: client sensor data update" << std::endl;
			    pthread_exit(NULL);
		    }
		}

		//Check battery 300 and seeking min_config
		if(battery_level <= 300 && test_battery_300 == false)
		{
		    test_battery_300 = true;
			pthread_t traitement;
			if(pthread_create(&traitement, NULL, seek_min_config, (void *) nodeList.at(0).c_str()) != 0) {
			    std::cerr << "[CAPTOR CLIENT] Error while creating a new thread: client sensor data update" << std::endl;
			    pthread_exit(NULL);
		    }
		
		}

	    usleep(CHECK_INTERVAL);
	}
}

// Procédure principale
int main(int argc, char* argv[]) {
	// Traitement des arguments
	if (argc == 0) { return -1; }

	for (int i = 0; i < argc; i++) {
		std::string strarg(argv[i]);

		if(strarg == "-a") {
			if (i < (argc - 1)) {
				ip_addr_df = argv[++i];
			} else {
				std::cerr << "[MAIN] Error: no specified ip address" << std::endl;
				exit(EXIT_FAILURE);
			}
		} else if(strarg == "-p") {
			if (i < (argc - 1)) {
				port = atoi(argv[++i]);
			} else {
				std::cerr << "[MAIN] Error: no specified port" << std::endl;
				exit(EXIT_FAILURE);
			}

		} else if(strarg == "-d") {
			if (i < (argc - 1)) {
				std::string dir(argv[++i]);
				dataDirectory = dir;
			} else {
				std::cerr << "[MAIN] Erreur: no specified data directory" << std::endl;
				exit(EXIT_FAILURE);
			}
		} else if(strarg == "-l") {
			++i;
			while (i < argc) {
				std::string noeud(argv[i]);
				nodeList.push_back(noeud);
				i++;
			}
		}
	}

	// threads
	pthread_t client,
			  listen;

	if (pthread_create(&client, NULL, clientFunc, NULL) != 0) {
		std::cerr << "[MAIN] Error while creating a new thread: client" << std::endl;
		pthread_exit(NULL);
	}

	if (pthread_create(&listen, NULL, listenServer, NULL) != 0) {
		std::cerr << "[MAIN] Error while creating a new thread: server" << std::endl;
		pthread_exit(NULL);
	}

	while(true) {

	    if (battery_level < 50 && test_battery_50 == false) {
	        test_battery_50 = true;
	        dataListMutex.lock();
	        for (std::string dataname : dataList) {
	            std::string filename;
	            filename = dataDirectory + dataname;
	            std::cout << "[MAIN] Delete file " << filename << std::endl;
	            remove(filename.c_str());
	            counter_lost_files ++;
	        }
	        dataListMutex.unlock();
	        dataList.clear();
	    }

	    if(battery_low == true) {
	        // la moyen de perte de liaison
	        average_lost_files_binding = ((double)(counter_generated_files - counter_sent_files)/counter_generated_files) * 100;
	        // moyen interne de collecteur
	        average_lost_files = ((double) counter_lost_files/counter_generated_files) * 100;
	        std::cout << "[CAPTOR INFO] ________________________________________________________________________"  << std::endl;
	        std::cout << "[CAPTOR INFO] ||                      THE END OF THE PROCESSOR                      ||"  << std::endl;
            std::cout << "[CAPTOR INFO] ||--------------------------------------------------------------------||"  << std::endl;
            std::cout << "[CAPTOR INFO] || Generated files : " << counter_generated_files  << std::endl;
            std::cout << "[CAPTOR INFO] || Sent files : " << counter_sent_files  << std::endl;
            std::cout << "[CAPTOR INFO] || Lost files : " << counter_lost_files << std::endl;
            std::cout << "[CAPTOR INFO] || Not sent files : " << dataList.size() << std::endl;
            std::cout << "[CAPTOR INFO] || Average of lost files -Captor- : " <<  average_lost_files << " % " << std::endl;
            std::cout << "[CAPTOR INFO] || Average of lost files between Captor and Collector : "<< average_lost_files_binding << " % " << std::endl;
            std::cout << "[CAPTOR INFO] ||____________________________________________________________________||"  << std::endl;
            std::cout << "[CAPTOR INFO] \\---------------------------------------------------------------------/"  << std::endl;
            break;
	    }
	}

	return EXIT_SUCCESS;
}
