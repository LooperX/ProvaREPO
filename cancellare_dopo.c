#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/sem.h>
#include <time.h> 
#include <string.h>
#include <fcntl.h> 
#include <sys/stat.h> 

// PROVA CUCCU

#define CL 3 
#define S 10
#define BUF_SIZE 256

/** STRUCT */
typedef union semun{	//Struct semctl
	int val;
	struct semid_ds *buf;
	unsigned short int *array;
}arg;

typedef enum {FALSE, TRUE} bool;

typedef struct{
	int grav_sintomo;
	char* sintomo/*[BUF_SIZE]*/;
}dati_malattia;

typedef struct {
	pid_t reparto_PID;
	char* sintomo/*[BUF_SIZE]*/;
}richiesta;


bool isOpen = TRUE;
bool flag = TRUE;
bool haveToRead = TRUE;

/** PROTOTIPI */
void chiudiOspedale();

/** MAIN */
int main(int argc, char** argv){
	remove("server_FIFO"); //Per collaudo.				// // // // // // // // // //	
	
	/** Variabili */
	FILE* fp;
	FILE* f_sintomi;
	int configInfo[CL];
	int i=0;
	int numRead;
	char buf[BUF_SIZE];
	dati_malattia mal[S];
	richiesta req;
	dati_malattia answ;
	union semun arg;
	
	/** Pipe */
	int fdp[2];
	pipe(fdp);	//IF ?? ?? ??

	/** Fifo */
	int fifo;
	int fifo_risposta;
	char nome_client_FIFO[BUF_SIZE];	

	/** Lettura config */
	i = 0;
	fp = fopen("config.txt", "r");
	if(fp == NULL){
		printf("Errore nell'apertura del file.\n");
		exit(EXIT_FAILURE);
	}
	while(fscanf(fp, "%*s %d", &configInfo[i]) != EOF) 	
		i++;											
	fclose(fp);

	/** Lettura sintomi */
	i = 0;
	fp = fopen("sintomi.txt", "r"); 
	if(fp == NULL){
		printf("Errore nell'apertura del file.\n");
		exit(EXIT_FAILURE);
	}
	while(fscanf(fp, "%s %d", mal[i].sintomo, &mal[i].grav_sintomo) != EOF)	
		i++;											
	fclose(fp);	

	/** semPazienti = 10 */
	int semPazienti = semget(IPC_PRIVATE, 1, 0777);
	if(semPazienti == -1){
		printf("Errore nella creazione del semaforo.\n");
		exit(EXIT_FAILURE);
	}
	arg.val = configInfo[0]; 
	if(semctl(semPazienti, 0, SETVAL, arg) == -1){
		printf("Errore nella semctl.\n");
		exit(EXIT_FAILURE);
	}else{
		semctl(semPazienti, 0, GETVAL, arg);
		printf("SEM_PAZIENTI : semaforo creato.\n");
	}
	
	/** pMutex = 1 */
	int pMutex = semget(IPC_PRIVATE, 1, 0777);
	if(pMutex == -1){
		printf("Errore nella creazione del semaforo.\n");
		exit(EXIT_FAILURE);
	}
	arg.val = 1; 
	if(semctl(pMutex, 0, SETVAL, arg) == -1){
		printf("Errore nella semctl.\n");
		exit(EXIT_FAILURE);
	}else{
		semctl(pMutex, 0, GETVAL, arg);
		printf("P_MUTEX : semaforo creato.\n");
	}
	
	/** Operazioni sem */
	struct sembuf waitZero = {0, 0, 0}; //Wait 0
	struct sembuf incSem = {0, 1, 0}; //Incrementa (+1).
	struct sembuf decSem = {0, -1, 0}; //Decrementa (-1).

	/** Gestione chiusura ospedale */
	alarm(configInfo[2]);
	signal(SIGALRM, chiudiOspedale);
	//Handler per SIGQUIT

	/** PID */
	pid_t paziente;
	pid_t triage;
	pid_t reparto[configInfo[1]];
	int nReparto = 0;
	
	/** ... Processi figli ... */
	
	/** TRIAGE */
	switch(triage = fork()){
		case -1:
		printf("Errore nella fork di TRIAGE.\n");
		exit(EXIT_FAILURE);
		case 0:
		printf("TRIAGE: processo creato. Creazione file.txt per gestione output.\n");
		FILE* f_triage;
		f_triage = fopen("output/triage.txt", "w+"); 
		if(f_triage == NULL){
			printf("Errore nell'apertura del file (f_triage).\n");
			exit(EXIT_FAILURE);
		}		
		fprintf(f_triage, "\t\t*** *** *** TRIAGE *** *** ***\n");

		/** Ciclo per la lettura (e poi, innestato, per la scrittura)... */

		//Chiudo front write del lettore [pipe]
		if(close(fdp[1]) == -1){
			printf("Errore chiusura front write del LETTORE.\n");
			exit(1);
		}
	
		//Creo la well known FIFO. [FIFO]
		if(mkfifo("server_FIFO", S_IRWXU | S_IRWXG | S_IRWXO) == -1){
			printf("Impossibile creare la FIFO.\n");
			exit(EXIT_FAILURE);
		}
		printf("TRIAGE: well-known FIFO creata.\n");
	
		semop(pMutex, &waitZero, 1); 						// // // // // // // // // // //
		int creoSoloUnaVolta = 0;						// // // // // // // // // // //

		while(isOpen == TRUE){
			numRead = read(fdp[0], buf, BUF_SIZE);
			sscanf(buf, "%s - %d", answ.sintomo, &answ.grav_sintomo);
			printf("T = Ho ricevuto: %s - %d\n", answ.sintomo, answ.grav_sintomo);
			if(numRead == -1){						// // // // // NON DOVREBBE ESSERE SU?
				printf("Errore nella lettura dal pipe.\n");
				exit(1);
			}
		
			/** Decido a che reparto assegnare */
			//
			//

			/** Comunico ai reparti... */
			if((fifo = open("server_FIFO", O_RDONLY)) == -1){//Apre FIFO in lettura.
				printf("Errore nella OPEN (RDONLY_server).\n");
				exit(EXIT_FAILURE);
			}
			printf("TRIAGE: pronto a ricevere ricihieste. [well known FIFO aperta]\n");
			
			if(read(fifo, &req, sizeof(richiesta)) == -1){
				printf("Errore nella READ.\n");
				exit(1);		
			}
			printf("TRIAGE: ricevuta richiesta da REPARTO_%d. Malattia(e) richista(e): %d - %s", nReparto+1, req.reparto_PID, req.sintomo);
			
			answ.sintomo = req.sintomo;
			answ.grav_sintomo = 100;			// // // // // // // // // //			
			
			if(creoSoloUnaVolta < 2){			// // // // // // // // // //
				sprintf(nome_client_FIFO, "FIFO_%d", req.reparto_PID);
				if(mkfifo(nome_client_FIFO, S_IRWXU | S_IRWXG | S_IRWXO) == -1){
					printf("Impossibile creare la FIFO_CLIENT.\n");
					exit(EXIT_FAILURE);
				}
				creoSoloUnaVolta++;
			}
			
			if((fifo_risposta = open(nome_client_FIFO, O_WRONLY)) == -1){ // // // // // // // // // //
				printf("Errore nella OPEN (WRONLY_server).\n");
				exit(EXIT_FAILURE);
			}

			if(write(fifo_risposta, &answ, sizeof(dati_malattia)) == -1){
				printf("Errore nella WRITE (Server).\n");
				exit(1);
			}

			if(close(fifo_risposta) == -1){//EOF
				printf("Errore nella CHIUSURA (dopo WRITE)\n");
				exit(1);
			}						
		}
		//Chiudo il front read (pipe)
		if(close(fdp[0] == -1)){
			printf("Errore nella chiusura del front read del LETTORE.\n");
			exit(1);
		}		
		//		
		//		
		//	
		//
		fclose(f_triage);
		exit(0);
	}

	/** REPARTI */
	char nome_file_rep[100];
	while(nReparto < configInfo[1]){
		switch(reparto[nReparto] = fork()){
			case -1:
			printf("Errore nella fork di REPARTO %d\n", nReparto);
			exit(EXIT_FAILURE);
			case 0:
			printf("REPARTO_%d: processo creato. Creazione file.txt per gestione output.\n", nReparto+1);
			FILE* f_reparto[configInfo[1]];
			sprintf(nome_file_rep, "output/reparto_%d.txt", nReparto+1); //Compongo il nome del file.
			f_reparto[nReparto] = fopen(nome_file_rep, "w+"); 
			if(f_reparto[nReparto] == NULL){
				printf("Errore nell'apertura del file (f_reparto%d).\n", nReparto+1);
				exit(EXIT_FAILURE);
			}
			fprintf(f_reparto[nReparto], "\t\t*** *** *** REPARTO_%d *** *** ***\n", nReparto+1);
			semop(pMutex, &waitZero, 1); // // // // // // // // // // //
			//Client - Richiede dati
			while(isOpen == TRUE){
				if((fifo = open("server_FIFO", O_WRONLY)) == -1){//Apre FIFO in scrittura. // // // // // 
					printf("Errore nella OPEN.\n");
					exit(EXIT_FAILURE);
				}
				printf("REPARTO_%d: well known fifo aperta in scrittura.\n", nReparto+1);
			
				req.reparto_PID = getpid();
				req.sintomo = "provaprova";

				if(write(fifo, &req, sizeof(richiesta)) == -1){
					printf("Errore nella WRITE.\n");
					exit(1);
				}

				if(close(fifo) == -1){//EOF
					printf("Errore nella CHIUSURA (dopo WRITE)\n");
					exit(1);
				}

				sprintf(nome_client_FIFO, "FIFO_%d", getpid());
				//COME SYNCH!
				while((fifo_risposta = open(nome_client_FIFO, O_RDONLY)) < 0);

				if(read(fifo_risposta, &answ.grav_sintomo, sizeof(int)) == -1){	
					printf("Errore nella READ (client_uno).\n");
					exit(1);		
				}	
				printf("Se ho ricevuto stampo 100. Stampo: %d\n", answ.grav_sintomo);
				//
				//
				//
			}
			fclose(f_reparto[nReparto]);
			exit(0);
		}
		nReparto++;
	}		

	/** GENERATORE DI PROCESSI (aka PADRE) */
	
	//Variabili solo padre
	int rnd = 0;
	char comunicazione_pipe[BUF_SIZE];
	
	printf("GENERATORE PAZIENTI: processo creato. Creazione file.txt per gestione output.\n");
	FILE* f_generatore;
	f_generatore = fopen("output/generatore_processi.txt", "w+"); 
	if(f_generatore == NULL){
		printf("Errore nell'apertura del file (f_generatore).\n");
		exit(EXIT_FAILURE);
	}		
	fprintf(f_generatore, "\t\t*** *** *** GENERATORE PROCESSI *** *** ***\n");
	
	semop(pMutex, &decSem, 1); // // // // // // // // // // //
	//INIZIO CICLO - Genero un paziente.
	if(close(fdp[0]) == -1){
		printf("Errore chiusura front read dello SCRITTORE.\n");
		exit(1);
	}
	while(isOpen == TRUE){
		switch(paziente = fork()){
			case -1:
			printf("Errore nella fork PAZIENTE.\n");
			exit(EXIT_FAILURE);
			case 0:
			//PAZIENTE
			if(isOpen == TRUE){
				semop(semPazienti, &decSem, 1);
				sleep(1); 
				//Genero casualmente un sintomo e compongo la stringa da passare del tipo: "sintomo" - "gravità".
				srand(time(NULL)); 
				rnd = rand() % S;
				sprintf(comunicazione_pipe, "%s - %d", mal[rnd].sintomo, mal[rnd].grav_sintomo);
				printf("PAZIENTE: ho composto la stringa %s .\n", comunicazione_pipe);	//-> su f_generatore
				if (write(fdp[1], comunicazione_pipe, strlen(comunicazione_pipe)) != strlen(comunicazione_pipe)){
					printf("Errore nella WRITE.\n");
					exit(1);
				}
				if(close(fdp[1] == -1)){ 
					printf("Errore nella chiusura del front write dello SCRITTORE.\n");
					exit(1);
				}		
				printf("PAZIENTE: ho comunicato.\n");						  				
			}
			exit(0);
		}
	printf("GENERATORE PROCESSI: Attendo...\n");
	waitpid(paziente, 0, 0);
	}
	//		
	//	
	//
	fclose(f_generatore);
	//Wait su triage e reparti
	return 0;
}


/** FUNZIONI */
void chiudiOspedale(){
	isOpen = FALSE;
}



