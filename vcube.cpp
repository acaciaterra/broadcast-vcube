/*
=============================================================================
Codigo fonte: implementacao do broadcast confiavel hierarquico com V-Cube.
Autora: Acacia dos Campos da Terra
Orientador: Prof. Elias P. Duarte Jr.
=============================================================================
*/

/*--------bibliotecas--------*/
#include <stdio.h>
#include <stdlib.h>
#include "smpl.h"
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <memory>

/*--------eventos--------*/
#define test 1
#define fault 2
#define repair 3
#define receive_msg 4
#define receive_ack 5
#define broadcast 6
#define new_msg 7

/*--------definicoes usadas pela funcao cisj--------*/
#define POW_2(num) (1<<(num)) //equivale a 2 elevado a num (sendo num o tamanho dos clusters)
#define VALID_J(j, s) ((POW_2(s-1)) >= j) //verifica se j eh um indice valido para acessar no cluster

/*--------estrutura das mensagens(tree e ack)--------*/
typedef struct {
	char type; //2 tipos: tree e ack (T e A)
	std:: string m; //mensagem m
	int idorigem; //id da origem
	int timestamp; //contador sequencial
}tmsg;
std:: vector<tmsg> mensagens;

//ultima mensagem utilizada pelo programa (para enviar/receber)
auto ultima_msg = std::make_unique<tmsg>();

/*--------estrutura da tripla armazenada pelo ack_set--------*/
struct tset{
	int idorigem, iddestino;
	tmsg m;

	bool operator<(const tset &outro) const{
		if(idorigem < outro.idorigem)
			return true;
		if(outro.idorigem < idorigem)
			return false;
		if(iddestino < outro.iddestino)
			return true;
		if(outro.iddestino < iddestino)
			return false;
		return m.m < outro.m.m;
	}
};

/*--------descritor do nodo--------*/
typedef struct{
	int id; //cada nodo tem um id
	int idr; //id real!!!
	int *state; //vetor state do nodo
	std:: vector<tmsg> last; //vetor com a ultima mensagem recebida de cada processo fonte
	std:: set<tset> ack_set; //set de tset
}tnodo;
std:: vector<tnodo> nodo;

/*--------estrutura do vetor de testes--------*/
typedef struct{
	int *vector; //cada nodo retornara um vetor de inteiros
	int size; //tamanho do vetor
	int *etapa;//o s da funcao cis
} testsvector;
testsvector *tests;

/*--------estrutura usada pela funcao cisj--------*/
typedef struct node_set {
	int* nodes;
	ssize_t size;
	ssize_t offset;
} node_set;

int token, N, timestamp = 0, s, ultimo_nodo;
/*--------funcoes usadas pelo cisj--------*/
static node_set*
set_new(ssize_t size)
{
	node_set* newn;

	newn = (node_set*)malloc(sizeof(node_set));
	newn->nodes = (int*)malloc(sizeof(int)*size);
	newn->size = size;
	newn->offset = 0;
	return newn;
}

static void
set_insert(node_set* nodes, int node)
{
	if (nodes == NULL) return;
	nodes->nodes[nodes->offset++] = node;
}

static void
set_merge(node_set* dest, node_set* source)
{
	if (dest == NULL || source == NULL) return;
	memcpy(&(dest->nodes[dest->offset]), source->nodes, sizeof(int)*source->size);
	dest->offset += source->size;
}

static void
set_free(node_set* nodes)
{
	free(nodes->nodes);
	free(nodes);
}
/* node_set.h --| */

node_set*
cis(int i, int s)
{
	node_set* nodes, *other_nodes;
	int xorr = i ^ POW_2(s-1);
	int j;

	/* starting node list */
	nodes = set_new(POW_2(s-1));

	/* inserting the first value (i XOR 2^^(s-1)) */
	set_insert(nodes, xorr);

	/* recursion */
	for (j=1; j<=s-1; j++) {
		other_nodes = cis(xorr, j);
		set_merge(nodes, other_nodes);
		set_free(other_nodes);
	}
	return nodes;
}
/*--------fim das funcoes do cisj--------*/


/*--------funcao que encontra qual sera o nodo testador--------*/
//nesta funcao sao encontrados os testadores de cada nodo
//(primeiro nodo SEM-FALHA de cada cluster em cada etapa)
int findtester (int i, int s) {
	int aux = 0;
	node_set* cluster = cis(i, s);
	// printf("---------> i: %d s: %d cluster: %d\n", i, s, cluster->offset);
	for(int j = 0; j < cluster->size; j++){ //for para percorrer todo o cluster
		if(status(nodo[cluster->nodes[j]].id) == 0){//verifica se o nodo encontrado eh sem-falha
			//ira retornar o primeiro do cluster que esta SEM-FALHA
			aux = cluster->nodes[j];
			free(cluster); //libera a memoria
			return aux;
		}
	}
	//se acabar o for, eh porque todos os nodos do cluster estao falhos
	free(cluster);
	return -2;
}


/*--------funcao que retorna os testes que serao executados pelo nodo--------*/
testsvector* testes(int n, int s, std:: vector<tnodo> &nodo) {
	//n = numero de nodos
	//s = log de n (numero de rodadas de teste [cada linha da tabela])

	int nodoTestador, position = 0;
	testsvector *tests = (testsvector*) malloc(sizeof(testsvector)*n);

	for(int k = 0; k < n; k++){
		tests[k].vector = (int*) malloc(sizeof(int)*n);//aloca o vetor com n posicoes
		tests[k].etapa = (int*) malloc(sizeof(int)*n);
		if(status(nodo[k].id) == 1) {
			tests[k].size = 0;
			continue;
		}//se for falho, nao faz a busca por testadores

		for(int i = 0; i < n; i++){ //for que percorre todos os nodos
			for(int j = 1; j <= s; j++){ //for para percorrer todas as etapas
				nodoTestador = findtester(i, j); //encontra testador do nodo

				//se o nodo for igual a k, entao adiciona a lista de testadores daquele nodo
				//significa que eh o testador que estamos procurando
				if(nodoTestador == k){
					// printf(">>>>> %d\n", j);
					tests[k].vector[position] = i;
					tests[k].etapa[position++] = j;
				}
			}
		}
		tests[k].size = position;
		position = 0;
	}
	return tests; //retorna o vetor com os testes executados
}
/*------------funcoes do broadcast-----------------*/

//funcao para checar os acks
void checkacks(tnodo j, tmsg msg){
	bool r;
	//existe algum elemento em ack_set que respeite <j, *, m>?
	//retorna true, caso exista
	r = std::any_of(nodo[token].ack_set.begin(), nodo[token].ack_set.end(), [j, msg] (auto const &elem){
		return elem.idorigem == j.idr && !(elem.m.m.compare(msg.m));
	});
	//if ack_set intersec {<j, *, m>} = vazio
	if(!r){
		// printf("Acabaram os acks pendentes do processo\n");
		//veririca se source(m) esta sem-falha e tambem o j para mim (i)
		if(nodo[token].state[msg.idorigem] % 2 == 0 && nodo[token].state[j.id] % 2 == 0){
			// send_ack(msg, j); //enviando ack para pj
			ultimo_nodo = token;
			schedule(receive_ack, 1.0, j.idr);
		}
	}
}

void print_tests(){
	for(int j = 0; j < N; j++){
		for(int i = 0; i < tests[j].size; i++){
			printf("> %d (s: %d) ", tests[j].vector[i], tests[j].etapa[i]);
		}
		printf("\n");
	}
}


void freceive(int enviou, int recebeu){
	int k, etp;
	bool res;
	// printf("<----Ultimo nodo - chegou na receive: %d\n", enviou);
	// printf("TYPE = %c\n", nodo[token].last[ultima_msg->idorigem].type);
	// std::cout << "-----> schedule:" << ultima_msg->m << "\n";
	// printf("ULTIMO NODO: %d TOKEN: %d\n", enviou, token);
	if(nodo[recebeu].last[ultima_msg->idorigem].type == 'N'
	|| ultima_msg->timestamp > nodo[recebeu].last[ultima_msg->idorigem].timestamp){
		//last_i[source(m)] <- m
		//Atualiza o last do nodo atual com a mensagem que acabou
		//de ser recebida do outro nodo
		nodo[recebeu].last[ultima_msg->idorigem].type = ultima_msg->type;
		nodo[recebeu].last[ultima_msg->idorigem].m.assign(ultima_msg->m);
		nodo[recebeu].last[ultima_msg->idorigem].idorigem = ultima_msg->idorigem;
		nodo[recebeu].last[ultima_msg->idorigem].timestamp = ultima_msg->timestamp;
		//deliver(m)
		std::cout <<"[DELIVER] mensagem \"" << nodo[recebeu].last[ultima_msg->idorigem].m << "\" recebida do nodo " << enviou <<" foi entregue para a aplicacao pelo processo " << recebeu << " no tempo {" << time() << "}\n\n";
		// printf("Nodo: %d\n", nodo[ultima_msg->idorigem].idr);
		// printf("Status do nodo: %d\n", status(nodo[ultima_msg->idorigem].id));
		if(status(nodo[ultima_msg->idorigem].id) != 0){
			// std::cout <<"Fazendo novo broadcast para os vizinhos sem-falha... " << "\n";
			schedule(broadcast, 1.0, recebeu);
			// broadcast(*ultima_msg);
			return;
		}
	}
	//encontrar identificador do cluster s de i que contem j
	for(int c = 0; c < tests[recebeu].size; c++){
		// printf("Agora vai calcular o cluster...\n");
		if(tests[recebeu].vector[c] == enviou){
			etp = tests[recebeu].etapa[c];
			// printf("Cluster do %d para o %d: %d\n", recebeu, enviou, etp);
			break;
		}
	}
	// print_tests();
	//retransmitir aos vizinhos corretos na arvore de source(m)
	//que pertencem ao cluster s que contém elementos
	//corretos de g

	for(int i = 0; i < N; i ++){
		for(int j = 0; j < tests[i].size; j++){
			for(int k = 1; k <= etp - 1; k++){
				if(tests[i].vector[j] == recebeu && tests[i].etapa[j] == k && status(nodo[i].id) == 0){
					// existe algum elemento em ack_set que respeite <j, k, m>?
					//retorna true, caso exista
					res = std::any_of(nodo[recebeu].ack_set.begin(), nodo[recebeu].ack_set.end(), [i, enviou] (auto const &elem){
						return elem.idorigem == enviou && elem.iddestino == nodo[i].idr && !(elem.m.m.compare(ultima_msg->m));
					});
					//if <j,k,m> nao pertence ack_set(i)
					// j = enviou, k = ff_neighbor (aqui eh o nodo[i])
					if(!res){
						nodo[recebeu].ack_set.insert(tset{enviou, nodo[i].idr, *ultima_msg});
						printf("[RBCAST] processo %d envia mensagem para processo %d\n", recebeu, nodo[i].idr);
						freceive(recebeu, nodo[i].idr);
					}
				}
			}
		}
	}

	// for(int i = 1; i <= etp - 1; i++){
	// 	//existe algum elemento em ack_set que respeite <*, k, m>?
	// 	//retorna true, caso exista
	// 	for(int j = 0; j < tests[recebeu].size; j++){
	// 		if(tests[recebeu].etapa[j] == i && status(nodo[tests[recebeu].vector[j]].id) == 0){
	// 			//k <- ff_neighbor
	// 			k = tests[recebeu].vector[j];
	// 			// printf("ff_neighbor::::: %d\n", k);
	// 			//existe algum elemento em ack_set que respeite <j, k, m>?
	// 			//retorna true, caso exista
	// 			res = std::any_of(nodo[recebeu].ack_set.begin(), nodo[recebeu].ack_set.end(), [k, enviou] (auto const &elem){
	// 				return elem.idorigem == enviou && elem.iddestino == k && !(elem.m.m.compare(ultima_msg->m));
	// 			});
	// 			//if <j,k,m> nao pertence ack_set(i)
	// 			if(!res){
	// 				nodo[recebeu].ack_set.insert(tset{enviou, k, *ultima_msg});
	// 				printf("[RBCAST] processo %d envia mensagem para processo %d\n", recebeu, k);
	// 				// enviou = recebeu;
	// 				// schedule(receive_msg, 1.0, k);
	// 				// ja_enviou[k] = 1;
	// 				freceive(recebeu, k);
	// 			}
	// 		}
	// 	}
	// }
	checkacks(nodo[enviou], *ultima_msg);
}

//funcao chamada quando o processo i detecta o j como falho
void crash(tnodo j){
	int etp = 0, k = 0;
	bool r;
	//IF remover acks pendentes para <j,*,*>
	//ELSE enviar m para vizinho k, se k existir
	for(std:: set<tset>::iterator cont = nodo[token].ack_set.begin(); cont != nodo[token].ack_set.end(); cont++){
		if(cont->idorigem == j.idr)
			nodo[token].ack_set.erase(cont);
		else if(cont->iddestino == j.idr){
			//encontrar identificador do cluster s de i que contem j
			for(int c = 0; c < tests[token].size; c++)
				if(tests[token].vector[c] == j.idr){
					etp = tests[token].etapa[c];
					break;
				}
			for(std:: set<tset>::iterator cont2 = nodo[token].ack_set.begin(); cont2 != nodo[token].ack_set.end(); cont2++){
				//Se a mensagem eh a mesma que estamos analisando no for mais externo
				//se o nodo destino dessa mensagem achada esta sem-falha
				//e se esse nodo esta no mesmo cluster (s) que o nodo que falhou estava no nodo i
				if(!(cont2->m.m.compare(cont->m.m)) && status(nodo[cont2->iddestino].id) == 0 && tests[token].etapa[cont2->iddestino] == etp){
					//k <- ff_neighbor_i(s)
					for(int i = 0; i < tests[token].size; i++){
						if(tests[token].etapa[i] == etp && status(nodo[tests[token].vector[i]].id) == 0){
							k = tests[token].vector[i];
							// printf("Encontrou o ff_neighbor, que eh: %d", k);
							break;
						}
					}
					// printf("Vai verificar se alguem mandou mensagem para o ff_neighbor\n");
					//existe algum elemento em ack_set que respeite <*, k, m>?
					//retorna true, caso exista
					// --- cont eh do ..primeiro for, que esta passando por todos os elementos do ack_set
					// --- k eh o primeiro nodo sem falha obtido pela ff_neighbor
					r = std::any_of(nodo[token].ack_set.begin(), nodo[token].ack_set.end(), [cont, k] (auto const &elem){
						return elem.idorigem == cont->idorigem && elem.iddestino == k && !(elem.m.m.compare(cont->m.m));
					});
					//se nao encontrou o elemento, adiciona
					if(!r){
						// printf("Ninguem mandou :( vamos mandar agora\n");
						nodo[token].ack_set.insert(tset{cont->idorigem, k, cont->m});
						// send_msg(cont->m.m, cont2->iddestino, nodo[k]);
						//VERIFICAR MUUUUUUUUUUUITO ESSA FUNCAO HAHAHAH
						printf("--> Enviando mensagem do nodo %d para o nodo %d apos detectar %d falho\n", token, k, j.idr);
						freceive(token, k);
					}
				}
			}
		}
	}
	//fora do for
	//garantir a entrega da mensagem mais atual a todos os processos corretos
	//em dest(m) da ultima mensagem recebida de j (processo que falhou)
	//if last(i)[j] != vazio
	// printf("Chega aqui, pelo menos\n");
	// std::cout << nodo[token].last[j.idr].type << "\n";
	if(nodo[token].last[j.idr].type != 'N'){
		// printf("Sera que entra no if?\n");
		//existe algum elemento em ack_set que respeite <*, i, m>?
		//retorna true, caso exista
		//--- existe no ack_set de j (nodo falho) o registro de que eu (i) sou
		//um destinatario da ultima mensagem que eu recebi de j?
		//i E dest(last[j])
		r = std::any_of(j.ack_set.begin(), j.ack_set.end(), [j] (auto const &elem){
			return elem.iddestino == token && !(elem.m.m.compare(nodo[token].last[j.idr].m));
		});
		if(r){
			// printf("Aqui ele deve enviar a mensagem pro proximo do cluster\n");
			// broadcast(nodo[token].last[j.idr]);
			ultima_msg->type = nodo[token].last[j.idr].type;
			ultima_msg->m.assign(nodo[token].last[j.idr].m);
			ultima_msg->idorigem = nodo[token].last[j.idr].idorigem;
			ultima_msg->timestamp = nodo[token].last[j.idr].timestamp;
			// printf("Broadcast do crash\n");
			schedule(broadcast, 5.0, token);
		}
	}
}

/*---------------fim funcoes broadcast-----------------*/


//funcao que printa o vetor state de todos os nodos
void print_state(std:: vector<tnodo> &nodo, int n){

	// for(int i = 0; i < n; i++)
	// 	if(status(nodo[i].id) == 0)
	// 		printf("nodo sem falha %d\n", i);


	printf ("\nVetor STATE(i): ");
	for (int i = 0; i < n; i++)
		printf ("%d  ", i);
	printf ("\n");
	printf ("-------------------------------------\n");

	for (int i = 0; i < n; i++){
		if(status(nodo[i].id) == 0){
			printf (">     Nodo %d | ", i);
			for (int j = 0; j < n; j++)
					if(j < 9)
						printf (" %d ", nodo[i].state[j]);
					else
						printf ("  %d ", nodo[i].state[j]);
			printf("\n");
		}
	}
	printf("\n");
}

void print_init(){
	printf ("===========================================================\n");
	printf ("         Execucao do algoritmo V-Cube\n");
	printf ("         Aluna: Acacia dos Campos da Terra\n");
	printf ("         Professor: Elias P. Duarte Jr.\n");
	printf ("===========================================================\n\n");
}

void print_end(int r, int n){
	//apos o fim do tempo, printa os vetores states
	printf("\n--------------------------------------------------------------\n");
	printf("                       RESULTADOS\n");
	printf("\nNúmero de rodadas total do programa: %d\n", r);
	printf("\nVetor STATE ao final do diagnostico:\n");
	print_state(nodo, n);
}


int main(int argc, char const *argv[]) {
	static int event, r, i, aux, logtype, etp, k, x;
	static int nodosemfalha = 0, nodofalho = 0, nrodadas = 0, ntestes = 0, totalrodadas = 0;
	static char fa_name[5];	//facility representa o objeto simulado
	bool res, resb = true;
	int idx = -1;

//-------variáveis dos eventos passados por arquivo---------
	char evento[7];
	int processo;
	float tempo;

	 if(argc != 3){
		puts("Uso correto: ./vcube <arquivo> -parametro (-r ou -c)");
		exit(1);
	}

	//logtype, if 0 - reduced, if 1 - complete
	if(strcmp("-c", argv[2]) == 0)
		logtype = 1;
	else if(strcmp("-r", argv[2]) == 0)
		logtype = 0;
	else{
		printf("Parametro incorreto (-c ou -r)\n");
		exit(1);
	}

	//o arquivo foi chamado de tp por nenhum motivo especifico
	//faz a leitura do numero de nodos
	FILE *tp = fopen(argv[1], "r");
	if (tp != NULL)
		fscanf(tp, "%d\n", &N);
	else
		printf("Erro ao ler arquivo\n");
	fclose(tp);

	smpl(0, "programa vcube");
	reset();
	stream(1);
	// nodo = (tnodo*) malloc(sizeof(tnodo)*N);
	nodo.resize(N);

	for (i = 0; i < N; i++) {
	 	memset(fa_name, '\0', 5);
	 	printf(fa_name, "%d", i);
	 	nodo[i].id = facility(fa_name, 1);
		nodo[i].idr = i;
		//para cada nodo cria e inicializa vetor state com -1(UNKNOWN)
		nodo[i].state = (int*) malloc (sizeof(int)*N);
		//para cada nodo cria vetor last
		nodo[i].last.resize(N);
		for (int j = 0; j < N; j++){
			nodo[i].state[j] = 0;
			nodo[i].last[j].type = 'N';
			nodo[i].last[j].idorigem = -1;
			nodo[i].last[j].timestamp = -1;
		}
	 }

	 print_init();
	 /*schedule inicial*/
	 for (i = 0; i < N; i++)
	 	schedule(test, 30.0, i);

	tp = fopen(argv[1], "r");
	fscanf(tp, "%d\n", &N);
	while(!feof(tp)){
		fscanf(tp, "%s %f %d\n", evento, &tempo, &processo);
		// printf("%s %f %d\n", evento, tempo, processo);
		schedule((strcmp("fault", evento) == 0 ? fault : (strcmp("repair", evento) == 0 ? repair : (strcmp("broadcast", evento) == 0 ? broadcast : (strcmp("new_msg", evento) == 0 ? new_msg : test)))), tempo, processo);
		//escalona os eventos. Faz a verificação de string pois o schedule não aceita string como parâmetro
	}
	fclose(tp);

	aux = N;
	 printf("Programa inicializa - todos os nodos estao *sem-falha*\n");
	 for(s = 0; aux != 1; aux /= 2, s++);//for para calcular o valor de S (log de n)
	 tests = testes(N, s, nodo); //calcula quais os testes executados
	//  for(i = 0; i < N; i++){//printa os testes executados por cada nodo, apos ser calculado
	// 	 printf("O nodo %d testa os nodos: ", i);
	// 	 for(int j = 0; j < tests[i].size; j++)
	// 		 printf("%d ", tests[i].vector[j]);
	// 	 printf("\n");
	//  }



	// schedule(broadcast, 1.0, token);

	 while(time() < 250.0) {
	 	cause(&event, &token); //causa o proximo evento
	 	switch(event) {
	 		case test:
	 		if(status(nodo[token].id) != 0){
				// nodofalho++;  //contabiliza nodos FALHOS
				break; //se o nodo for falho, então quebra
			}else{
				nodosemfalha++;  //contabiliza nodos SEM-FALHA
			}

			//se o state de token for um numero impar, esta desatualizado
			//esta marcando como falho (impar = falho, par = sem-falha)
			//acrescenta-se +1, para indicar que esta sem-falha e ficar certo
			if((nodo[token].state[token] % 2) != 0)
				nodo[token].state[token]++;
			printf("[%5.1f] ", time());
			printf("Nodo %d testa: ", token);
			for(int i = 0; i < tests[token].size; i++){
				printf("%d ", tests[token].vector[i]);
				if(status(nodo[tests[token].vector[i]].id) == 0) {//se o nodo estiver sem-falha
					ntestes++;  //contabiliza os testes realizados

					//atualiza o valor no state, caso esteja desatualizado
					if((nodo[token].state[tests[token].vector[i]] % 2) != 0){
						if(nodo[token].state[tests[token].vector[i]] != -1)
							printf("(nodo detecta evento: nodo %d sem-falha) ", tests[token].vector[i]);
						nodo[token].state[tests[token].vector[i]]++;
					}

					for(int j = 0; j < N; j++){//for para verificar as novidades
						if(nodo[token].state[j] == -1){
							//caso seja o inicio do programa, atualiza o state com 0
							nodo[token].state[j] = 0;
						}else if(nodo[token].state[j] < nodo[tests[token].vector[i]].state[j]){
							//caso nao seja o inicio e o valor do state do token seja menor
							//que o do state de j, entao copia as novidades
							nodo[token].state[j] = nodo[tests[token].vector[i]].state[j];
							printf("(nodo %d obtem info nodo %d: nodo %d falho) ", token, tests[token].vector[i], j);

							crash(nodo[j]);
						}
					}
				} else if((nodo[token].state[tests[token].vector[i]] % 2) == 0) {
					//caso o nodo esteja falho e o state esteja desatualizado
					//ou seja, esta como nao falho, mas na verdade esta falho
					//entao atualiza o vetor state
					nodo[token].state[tests[token].vector[i]]++;
					printf("(nodo detecta evento: nodo %d falho) ", tests[token].vector[i]);

					// printf("------- %d\n", nodo[l].idr);
					//envia o nodo que falhou
					crash(nodo[tests[token].vector[i]]);
				}
			}
			printf("\n");


			// printf("\n\t\t>>> nodo falho: %d nodo sem falha: %d\n\n", nodofalho, nodosemfalha);
// -------------------------------verificacao para numero de rodadas-------------------------------------------------------------
			if((nodofalho + nodosemfalha == N) && time() > 30.0){  //so entra se todos foram testados
				int nodosf, i, end = 1;
				nodosemfalha = 0;

				for (nodosf = 0; nodosf < N; nodosf++){  //encontra o primeiro nodo SEM-FALHA
					if (status(nodo[nodosf].id) == 0)
						break;
				}

				if (nodosf != N-1){ //verifica se nao eh o ultimo, para evitar seg fault
					for (i = nodosf + 1; i < N; i++){ //compara o vetor de nodosf com os demais
						if (status(nodo[i].id) != 0)//se for nodo FALHO apenas passa para o proximo
							continue;

						for (int j = 0; j < N; j++){  //compara se state dos SEM-FALHA sao iguais
							if (nodo[nodosf].state[j] != nodo[i].state[j])
								end = 0;//significa que os vetores estao diferentes
						}
					}
				}
				nrodadas++;//aumenta o numero de rodadas, independente dos vetores estarem iguais
				totalrodadas++;//aumenta as rodadas do programa todo
				if(logtype)
					print_state(nodo, N);
				printf("\t------ Fim da rodada %d ------\n", totalrodadas);

				if (i == N && end == 1){
					//todos os vetores SEM-FALHA foram comparados e sao iguais
					//entao o evento foi diagnosticado, printa a quantidade de rodadas e testes necessarios
					// printf ("O evento precisou de %d rodadas e %d testes para ser diagnosticado\n", nrodadas, ntestes);
				}
				// print_tests();
			}

// ---------------------------------------fim da verificacao de rodadas-----------------------------------------------------

	 		schedule(test, 30.0, token);
	 		break;

	 		case fault:
			// print_state(nodo, N);
			//atualiza os valores para continuar contando
			nodosemfalha = 0;
			nodofalho++;
			nrodadas = 0;
			ntestes = 0;
	 		r = request(nodo[token].id, token, 0);
	 		if(r != 0) {
	 			puts("Impossível falhar nodo");
	 			exit(1);
	 		}
				printf("EVENTO: O nodo %d falhou no tempo %5.1f\n", token, time());

//-----------------------a cada evento, recalcula o vetor tests----------------------------------
			free(tests);
			tests = testes(N, s, nodo); //calcula quais os testes executados
			// for(i = 0; i < N; i++){//printa os testes executados por cada nodo, apos ser calculado
			// 	printf("O nodo %d testa os nodos: ", i);
			// 	for(int j = 0; j < tests[i].size; j++)
			// 		printf("%d ", tests[i].vector[j]);
			// 	printf("\n");
			// }
			break;

	 		case repair:
			// print_state(nodo, N);
			//atualiza os valores para continuar contando
			nodofalho--; //se recuperou, tem um nodo falho a menos
			nodosemfalha = 0;
			nrodadas = 0;
			ntestes = 0;

	 		release(nodo[token].id, token);
	 		printf("EVENTO: O nodo %d recuperou no tempo %5.1f\n", token, time());

//-----------------------a cada evento, recalcula o vetor tests----------------------------------
			free(tests);
			tests = testes(N, s, nodo); //calcula quais os testes executados
			// for(i = 0; i < N; i++){//printa os testes executados por cada nodo, apos ser calculado
			// 	printf("O nodo %d testa os nodos: ", i);
			// 	for(int j = 0; j < tests[i].size; j++)
			// 		printf("%d ", tests[i].vector[j]);
			// 	printf("\n");
			// }
			schedule(test, 30.0, token);
			break;
			case receive_msg:
				// int k;
				// // printf("<----Ultimo nodo - chegou na receive: %d\n", ultimo_nodo);
				// // printf("TYPE = %c\n", nodo[token].last[ultima_msg->idorigem].type);
				// // std::cout << "-----> schedule:" << ultima_msg->m << "\n";
				// // printf("ULTIMO NODO: %d TOKEN: %d\n", ultimo_nodo, token);
				// if(nodo[token].last[ultima_msg->idorigem].type == 'N'
				// || ultima_msg->timestamp > nodo[token].last[ultima_msg->idorigem].timestamp){
				// 	//last_i[source(m)] <- m
				// 	//Atualiza o last do nodo atual com a mensagem que acabou
				// 	//de ser recebida do outro nodo
				// 	nodo[token].last[ultima_msg->idorigem].type = ultima_msg->type;
				// 	nodo[token].last[ultima_msg->idorigem].m.assign(ultima_msg->m);
				// 	nodo[token].last[ultima_msg->idorigem].idorigem = ultima_msg->idorigem;
				// 	nodo[token].last[ultima_msg->idorigem].timestamp = ultima_msg->timestamp;
				// 	//deliver(m)
				// 	std::cout <<"MENSAGEM " << nodo[token].last[ultima_msg->idorigem].m << " RECEBIDA DO NODO " << ultimo_nodo <<" FOI ENTREGUE PARA A APLICACAO (DELIVER) PELO PROCESSO " << token << " NO TEMPO " << time() << "\n";
				// 	// printf("Nodo: %d\n", nodo[ultima_msg->idorigem].idr);
				// 	// printf("Status do nodo: %d\n", status(nodo[ultima_msg->idorigem].id));
				// 	if(status(nodo[ultima_msg->idorigem].id) != 0){
				// 		// std::cout <<"Fazendo novo broadcast para os vizinhos sem-falha... " << "\n";
				// 		schedule(broadcast, 1.0, token);
				// 		// broadcast(*ultima_msg);
				// 		break;
				// 	}
				// }
				// //encontrar identificador do cluster s de i que contem j
				// for(int c = 0; c < tests[token].size; c++){
				// 	printf("Agora vai calcular o cluster...\n");
				// 	if(tests[token].vector[c] == ultimo_nodo){
				// 		etp = tests[token].etapa[c];
				// 		printf("Cluster do %d: %d\n", ultimo_nodo, etp);
				// 		break;
				// 	}
				// }
				// //retransmitir aos vizinhos corretos na arvore de source(m)
				// //que pertencem ao cluster s que contém elementos
				// //corretos de g
				// printf("A principio, o cluster do %d é %d\n", token, etp);
				// printf("O nodo token é: %d\n", token);
				// printf("Se for 2, ou mais, ele PRECISA ENTRAR NO FOR\n");
				// for(int i = 1; i <= etp - 1; i++){
				// 	printf("Entrou no for :D\n");
				// 	//existe algum elemento em ack_set que respeite <*, k, m>?
				// 	//retorna true, caso exista
				// 	for(int j = 0; j < tests[token].size; j++){
				// 		if(tests[token].etapa[j] == i && status(nodo[tests[token].vector[j]].id) == 0){
				// 			//k <- ff_neighbor
				// 			k = tests[token].vector[j];
				// 			//existe algum elemento em ack_set que respeite <j, k, m>?
				// 			//retorna true, caso exista
				// 			res = std::any_of(nodo[token].ack_set.begin(), nodo[token].ack_set.end(), [k] (auto const &elem){
				// 				return elem.idorigem == ultimo_nodo && elem.iddestino == k && !(elem.m.m.compare(ultima_msg->m));
				// 			});
				// 			//if <j,k,m> nao pertence ack_set(i)
				// 			if(!res){
				// 				nodo[token].ack_set.insert(tset{ultimo_nodo, k, *ultima_msg});
				// 				printf("%d Enviou a mensagem para o processo %d\n", token, k);
				// 				// ultimo_nodo = token;
				// 				schedule(receive_msg, 1.0, k);
				// 			}
				// 		}
				// 	}
				// }
				// checkacks(nodo[k], *ultima_msg);
				break;
			case receive_ack:
				// x | <x,j,m> pertence ack_set(i)
				for(std:: set<tset>::iterator cont = nodo[token].ack_set.begin(); cont != nodo[token].ack_set.end(); cont++){
					// std::cout << "Origem: " << cont->idorigem << " Destino: " << cont->iddestino << "\n";
					if(cont->iddestino == ultimo_nodo && !(cont->m.m.compare(ultima_msg->m))){
						idx = cont->idorigem;
					}
				}
				//if meu id != x, identifica nodo e chama checkacks
				if(idx != -1 && idx != nodo[token].idr){
					// printf("%d\n", idx);
					checkacks(nodo[idx], *ultima_msg);
				}
				break;

			case new_msg:
			int tam;
				//armazena em mensagens todas as mensagens ja enviadas pelo broadcast,
				//contendo o id de origem e o timestamp (unico para cada mensagem)
				//--- transforma a string em um tmsg ---
				mensagens.push_back(tmsg{'T', "teste "+ std::to_string(timestamp), nodo[token].idr, timestamp});
				tam = mensagens.size()-1;
				// std::cout << "Mensagem armazenada::::::::::::::::::" << mensagens[mensagens.size()-1].m << "\n";

				ultima_msg->type = mensagens[tam].type;
				ultima_msg->m.assign(mensagens[tam].m);
				ultima_msg->idorigem = mensagens[tam].idorigem;
				ultima_msg->timestamp = mensagens[tam].timestamp;
				//incrementa o timestamp sempre que gera uma nova mensagem
				timestamp++;
				break;


			case broadcast:
			//if source(m) == i
			bool res2, res3;
			if(ultima_msg->idorigem == token && status(nodo[token].id) == 0){
				//isso aqui não vai gerar look infinito não? HAHAHAHAHAHAHAHAHAH
				while(resb){
					//existe algum elemento em ack_set que respeite <i, *, last_i[i]>?
					//retorna true, caso exista
					resb = std::any_of(nodo[token].ack_set.begin(), nodo[token].ack_set.end(), [] (auto const &elem){
						return elem.idorigem == token && !(elem.m.m.compare(nodo[token].last[token].m)) && elem.m.idorigem == nodo[token].last[token].idorigem;
					});
				}

				//last_i[i] = m
				nodo[token].last[token].type = ultima_msg->type;
				nodo[token].last[token].m.assign(ultima_msg->m);
				nodo[token].last[token].idorigem = ultima_msg->idorigem;
				nodo[token].last[token].timestamp = ultima_msg->timestamp;

				//deliver(m)
				std::cout <<"[DELIVER] mensagem \"" << nodo[token].last[token].m << "\" foi entregue para a aplicacao pelo processo " << token << "\n\n";


			}
			//enviar a todos os vizinhos corretos que pertencem ao cluster s
			for(int i = 0; i < N; i ++){
				for(int j = 0; j < tests[i].size; j++){
					if(tests[i].vector[j] == token){
						// printf("token: %d\n", token);
						// printf("i: %d\n", i);
						printf("[RBCAST] processo %d envia mensagem para processo %d\n", token, nodo[i].idr);
						freceive(token, nodo[i].idr);
					}
				}
			}

			// for(int i = 1; i <= s; i++){
			// 	for(int j = 0; j < tests[token].size; j++){
			// 		// printf("{{{{{ i: %d nodo[tests[token].vector[j]].idr: %d\n", i, nodo[tests[token].vector[j]].idr);
			//
			// 		if(tests[token].etapa[j] == i && status(nodo[tests[token].vector[j]].id) == 0){
			// 			//j <- ff_neighbor(s)
			// 			k = nodo[tests[token].vector[j]].idr;
			// 			if(ja_enviou[k] == 0){
			// 				nodo[token].ack_set.insert(tset{token, k, *ultima_msg});
			// 				ja_enviou[k] = 1;
			// 				printf("[RBCAST] processo %d envia mensagem para processo %d\n", token, nodo[k].idr);
			// 				freceive(token, nodo[k].idr);
			// 			}
			// 		} else if(i != 1 && status(nodo[tests[token].vector[j]].id) != 0){
			// 			k = nodo[tests[token].vector[j]].idr + 1;
			// 			if(ja_enviou[k] == 0){
			// 				nodo[token].ack_set.insert(tset{token, k, *ultima_msg});
			// 				ja_enviou[k] = 1;
			// 				printf("[RBCAST] processo %d envia mensagem para processo %d\n", token, nodo[k].idr);
			// 				freceive(token, nodo[k].idr);
			// 			}
			// 		}
			// 	}
			// }

			break;

	 	}
	}

	print_end(totalrodadas, N);

	return 0;
}
