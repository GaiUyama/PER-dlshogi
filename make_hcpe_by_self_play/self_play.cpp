#include "init.hpp"
#include "position.hpp"
#include "usi.hpp"
#include "move.hpp"
#include "movePicker.hpp"
#include "generateMoves.hpp"
#include "search.hpp"
#include "tt.hpp"
#include "book.hpp"

#include <iostream>
#include <fstream>
#include <random>
#include <thread>
#include <mutex>
#include <memory>
#include "ZobristHash.h"
#include "mate.h"

#include "cppshogi.h"
namespace py = boost::python;
namespace np = boost::python::numpy;

// ���[�g�m�[�h�ł̋l�ݒT�����s��
//#define USE_MATE_ROOT_SEARCH

#define SPDLOG_TRACE_ON
#define SPDLOG_DEBUG_ON
#define SPDLOG_EOL "\n"
#include "spdlog/spdlog.h"
auto loggersink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
auto logger = std::make_shared<spdlog::async_logger>("selfplay", loggersink, 8192);

using namespace std;

// ���f���̃p�X
string model_path;

int playout_num = 1000;

const unsigned int uct_hash_size = 16384; // UCT�n�b�V���T�C�Y
int threads; // �X���b�h��
atomic<int> running_threads; // ���s���̒T���X���b�h��
thread **handle; // �X���b�h�̃n���h��

s64 teacherNodes; // ���t�ǖʐ�
std::atomic<s64> idx = 0;

ifstream ifs;
ofstream ofs;
mutex imutex;
mutex omutex;
size_t entryNum;

// ����̍ő吔(�Տ�S��)
const int UCT_CHILD_MAX = 593;

struct child_node_t {
	Move move;          // ���肷����W
	int move_count;     // �T����
	float win;          // ��������
	unsigned int index; // �C���f�b�N�X
	float nnrate;       // �j���[�����l�b�g���[�N�ł̃��[�g
};

struct uct_node_t {
	int move_count;
	float win;
	int child_num;                      // �q�m�[�h�̐�
	child_node_t child[UCT_CHILD_MAX];  // �q�m�[�h�̏��
	std::atomic<int> evaled; // 0:���]�� 1:�]���� 2:�����̉\������
	float value_win;
};

// 2�̃L���[�����݂Ɏg�p����
const int policy_value_batch_maxsize = 32; // �X���b�h���ȏ�m�ۂ���
static float features1[2][policy_value_batch_maxsize][ColorNum][MAX_FEATURES1_NUM][SquareNum];
static float features2[2][policy_value_batch_maxsize][MAX_FEATURES2_NUM][SquareNum];
struct policy_value_queue_node_t {
	uct_node_t* node;
	Color color;
};
static policy_value_queue_node_t policy_value_queue_node[2][policy_value_batch_maxsize];
static int current_policy_value_queue_index = 0;
static int current_policy_value_batch_index = 0;

mutex mutex_queue; // �L���[�̔r������
#define LOCK_QUEUE mutex_queue.lock();
#define UNLOCK_QUEUE mutex_queue.unlock();

// �\���֐�
py::object dlshogi_predict;

// �����_��
uniform_int_distribution<int> rnd(0, 999);

// ���[�m�[�h�ł̋l�ݒT���̐[��(��ł��邱��)
const int MATE_SEARCH_DEPTH = 7;

// �l�ݒT���ŋl�݂̏ꍇ��value_win�̒萔
const float VALUE_WIN = FLT_MAX;
const float VALUE_LOSE = -FLT_MAX;

unsigned const int NOT_EXPANDED = -1; // ���W�J�̃m�[�h�̃C���f�b�N�X

const float c_puct = 1.0f;


class UctSercher {
public:
	UctSercher(const int threadID) : threadID(threadID), mt(std::chrono::system_clock::now().time_since_epoch().count() + threadID) {
		// UCTHash
		uct_hash.InitializeUctHash(uct_hash_size);
		// UCT�̃m�[�h
		uct_node = new uct_node_t[uct_hash_size];
	}
	~UctSercher() {
		delete[] uct_node;
	}

	float UctSearch(Position *pos, unsigned int current, const int depth);
	int SelectMaxUcbChild(const Position *pos, unsigned int current, const int depth);
	unsigned int ExpandRoot(const Position *pos);
	unsigned int ExpandNode(Position *pos, unsigned int current, const int depth);
	bool InterruptionCheck(const unsigned int current_root, const int playout_count);
	void UpdateResult(child_node_t *child, float result, unsigned int current);
	void SelfPlay();


private:
	int threadID;
	UctHash uct_hash;
	uct_node_t* uct_node;
	std::mt19937 mt;
};

void randomMove(Position& pos, std::mt19937& mt);
static void QueuingNode(const Position *pos, unsigned int index, uct_node_t* uct_node, const Color color);


//////////////////////////////////////////////
//  UCT�T�����s���֐�                        //
//  1��̌Ăяo���ɂ�, 1�v���C�A�E�g����    //
//////////////////////////////////////////////
float
UctSercher::UctSearch(Position *pos, unsigned int current, const int depth)
{
	// �l�݂̃`�F�b�N
	if (uct_node[current].child_num == 0) {
		return 1.0f; // ���]���Ēl��Ԃ�����1��Ԃ�
	}
	else if (uct_node[current].value_win == VALUE_WIN) {
		// �l��
		return 0.0f;  // ���]���Ēl��Ԃ�����0��Ԃ�
	}
	else if (uct_node[current].value_win == VALUE_LOSE) {
		// ���ʂ̋l��
		return 1.0f; // ���]���Ēl��Ԃ�����1��Ԃ�
	}

	// �����`�F�b�N
	if (uct_node[current].evaled == 2) {
		switch (pos->isDraw(16)) {
		case NotRepetition: break;
		case RepetitionDraw: return 0.5f;
		case RepetitionWin: return 0.0f;
		case RepetitionLose: return 1.0f;
			// case RepetitionSuperior: if (ss->ply != 2) { return ScoreMateInMaxPly; } break;
			// case RepetitionInferior: if (ss->ply != 2) { return ScoreMatedInMaxPly; } break;
		default: UNREACHABLE;
		}
	}

	// policy���v�Z�����̂�҂�(���̃X���b�h�������m�[�h���ɓW�J�����ꍇ�Ann�̌v�Z��҂K�v������)
	while (uct_node[current].evaled == 0)
		this_thread::sleep_for(chrono::milliseconds(0));

	float result;
	unsigned int next_index;
	double score;
	child_node_t *uct_child = uct_node[current].child;

	// UCB�l�ő�̎�����߂�
	next_index = SelectMaxUcbChild(pos, current, depth);
	// �I�񂾎�𒅎�
	StateInfo st;
	pos->doMove(uct_child[next_index].move, st);

	// �m�[�h�̓W�J�̊m�F
	if (uct_child[next_index].index == NOT_EXPANDED) {
		// �m�[�h�̓W�J
		// �m�[�h�W�J�����̒���value���v�Z����
		unsigned int child_index = ExpandNode(pos, current, depth + 1);
		uct_child[next_index].index = child_index;
		//cerr << "value evaluated " << result << " " << v << " " << *value_result << endl;

		// �l�݃`�F�b�N(ValueNet�v�Z���Ƀ`�F�b�N)
		int isMate = 0;
		if (!pos->inCheck()) {
			if (mateMoveInOddPly(*pos, MATE_SEARCH_DEPTH)) {
				isMate = 1;
			}
		}
		else {
			if (mateMoveInEvenPly(*pos, MATE_SEARCH_DEPTH - 1)) {
				isMate = -1;
			}
		}

		// �����`�F�b�N
		int isDraw = 0;
		switch (pos->isDraw(16)) {
		case NotRepetition: break;
		case RepetitionDraw: isDraw = 2; break; // Draw
		case RepetitionWin: isDraw = 1; break;
		case RepetitionLose: isDraw = -1; break;
			// case RepetitionSuperior: if (ss->ply != 2) { return ScoreMateInMaxPly; } break;
			// case RepetitionInferior: if (ss->ply != 2) { return ScoreMatedInMaxPly; } break;
		default: UNREACHABLE;
		}

		// value���v�Z�����̂�҂�
		//cout << "wait value:" << child_index << ":" << uct_node[child_index].evaled << endl;
		while (uct_node[child_index].evaled == 0)
			this_thread::sleep_for(chrono::milliseconds(0));

		// �����̏ꍇ�AValueNet�̒l���g�p���Ȃ��i�o�H�ɂ���Ĕ��肪�قȂ邽�ߏ㏑���͂��Ȃ��j
		if (isDraw != 0) {
			uct_node[child_index].evaled = 2;
			if (isDraw == 1) {
				result = 0.0f;
			}
			else if (isDraw == -1) {
				result = 1.0f;
			}
			else {
				result = 0.5f;
			}

		}
		// �l�݂̏ꍇ�AValueNet�̒l���㏑��
		else if (isMate == 1) {
			uct_node[child_index].value_win = VALUE_WIN;
			result = 0.0f;
		}
		else if (isMate == -1) {
			uct_node[child_index].value_win = VALUE_LOSE;
			result = 1.0f;
		}
		else {
			// value�����s�Ƃ��ĕԂ�
			result = 1 - uct_node[child_index].value_win;
		}
	}
	else {
		// ��Ԃ����ւ���1��[���ǂ�
		result = UctSearch(pos, uct_child[next_index].index, depth + 1);
	}

	// �T�����ʂ̔��f
	UpdateResult(&uct_child[next_index], result, current);

	pos->undoMove(uct_child[next_index].move);
	return 1 - result;
}

//////////////////////
//  �T�����ʂ̍X�V  //
/////////////////////
void
UctSercher::UpdateResult(child_node_t *child, float result, unsigned int current)
{
	uct_node[current].win += result;
	uct_node[current].move_count++;
	child->win += result;
	child->move_count++;
}

/////////////////////////////////////////////////////
//  UCB���ő�ƂȂ�q�m�[�h�̃C���f�b�N�X��Ԃ��֐�  //
/////////////////////////////////////////////////////
int
UctSercher::SelectMaxUcbChild(const Position *pos, unsigned int current, const int depth)
{
	child_node_t *uct_child = uct_node[current].child;
	const int child_num = uct_node[current].child_num;
	int max_child = 0;
	const int sum = uct_node[current].move_count;
	float q, u, max_value;
	float ucb_value;
	unsigned int max_index;
	//const bool debug = GetDebugMessageMode() && current == current_root && sum % 100 == 0;

	max_value = -1;

	// UCB�l�ő�̎�����߂�  
	for (int i = 0; i < child_num; i++) {
		float win = uct_child[i].win;
		int move_count = uct_child[i].move_count;

		// evaled
		/*if (debug) {
		cerr << i << ":";
		cerr << uct_node[current].move_count << " ";
		cerr << setw(3) << uct_child[i].move.toUSI();
		cerr << ": move " << setw(5) << move_count << " policy "
		<< setw(10) << uct_child[i].nnrate << " ";
		}*/
		if (move_count == 0) {
			q = 0.5f;
			u = 1.0f;
		}
		else {
			q = win / move_count;
			u = sqrtf(sum) / (1 + move_count);
		}

		float rate = max(uct_child[i].nnrate, 0.01f);
		// �����_���Ɋm�����グ��
		if (depth == 0 && rnd(mt) <= 2) {
			rate = (rate + 1.0f) / 2.0f;
		}
		else if (depth < 4 && depth % 2 == 0 && rnd(mt) == 0) {
			rate = std::min(rate * 1.5f, 1.0f);
		}

		ucb_value = q + c_puct * u * rate;

		/*if (debug) {
		cerr << " Q:" << q << " U:" << c_puct * u * rate << " UCB:" << ucb_value << endl;
		}*/

		if (ucb_value > max_value) {
			max_value = ucb_value;
			max_child = i;
		}
	}

	/*if (debug) {
	cerr << "select node:" << current << " child:" << max_child << endl;
	}*/

	return max_child;
}

// ���f���ǂݍ���
void
ReadWeights()
{
	// Boost.Python��Boost.Numpy�̏�����
	Py_Initialize();
	np::initialize();

	// Python���W���[���ǂݍ���
	py::object dlshogi_ns = py::import("dlshogi.predict").attr("__dict__");

	// model���[�h
	py::object dlshogi_load_model = dlshogi_ns["load_model"];
	dlshogi_load_model(model_path.c_str());

	// �\���֐��擾
	dlshogi_predict = dlshogi_ns["predict"];
}

/////////////////////
//  ����̏�����  //
/////////////////////
static void
InitializeCandidate(child_node_t *uct_child, Move move)
{
	uct_child->move = move;
	uct_child->move_count = 0;
	uct_child->win = 0;
	uct_child->index = NOT_EXPANDED;
	uct_child->nnrate = 0;
}

/////////////////////////
//  ���[�g�m�[�h�̓W�J  //
/////////////////////////
unsigned int
UctSercher::ExpandRoot(const Position *pos)
{
	unsigned int index = uct_hash.FindSameHashIndex(pos->getKey(), pos->turn(), pos->gamePly());
	child_node_t *uct_child;
	int child_num = 0;

	// ���ɓW�J����Ă�������, �T�����ʂ��ė��p����
	if (index != uct_hash_size) {
		return index;
	}
	else {
		// ��̃C���f�b�N�X��T��
		index = uct_hash.SearchEmptyIndex(pos->getKey(), pos->turn(), pos->gamePly());

		assert(index != uct_hash_size);

		// ���[�g�m�[�h�̏�����
		uct_node[index].move_count = 0;
		uct_node[index].win = 0;
		uct_node[index].child_num = 0;
		uct_node[index].evaled = 0;
		uct_node[index].value_win = 0.0f;

		uct_child = uct_node[index].child;

		// ����̓W�J
		for (MoveList<Legal> ml(*pos); !ml.end(); ++ml) {
			InitializeCandidate(&uct_child[child_num], ml.move());
			child_num++;
		}

		// �q�m�[�h���̐ݒ�
		uct_node[index].child_num = child_num;

		// ����̃��[�e�B���O
		QueuingNode(pos, index, uct_node, pos->turn());

	}

	return index;
}

///////////////////
//  �m�[�h�̓W�J  //
///////////////////
unsigned int
UctSercher::ExpandNode(Position *pos, unsigned int current, const int depth)
{
	unsigned int index = uct_hash.FindSameHashIndex(pos->getKey(), pos->turn(), pos->gamePly() + depth);
	child_node_t *uct_child;

	// �����悪���m�ł����, �����Ԃ�
	if (index != uct_hash_size) {
		return index;
	}

	// ��̃C���f�b�N�X��T��
	index = uct_hash.SearchEmptyIndex(pos->getKey(), pos->turn(), pos->gamePly() + depth);

	assert(index != uct_hash_size);

	// ���݂̃m�[�h�̏�����
	uct_node[index].move_count = 0;
	uct_node[index].win = 0;
	uct_node[index].child_num = 0;
	uct_node[index].evaled = 0;
	uct_node[index].value_win = 0.0f;
	uct_child = uct_node[index].child;

	// ����̓W�J
	int child_num = 0;
	for (MoveList<Legal> ml(*pos); !ml.end(); ++ml) {
		InitializeCandidate(&uct_child[child_num], ml.move());
		child_num++;
	}

	// �q�m�[�h�̌���ݒ�
	uct_node[index].child_num = child_num;

	// ����̃��[�e�B���O
	if (child_num > 0) {
		QueuingNode(pos, index, uct_node, pos->turn());
	}
	else {
		uct_node[index].value_win = 0.0f;
		uct_node[index].evaled = 1;
	}

	return index;
}

//////////////////////////////////////
//  �m�[�h���L���[�ɒǉ�            //
//////////////////////////////////////
static void
QueuingNode(const Position *pos, unsigned int index, uct_node_t* uct_node, const Color color)
{
	LOCK_QUEUE;
	if (current_policy_value_batch_index >= policy_value_batch_maxsize) {
		std::cout << "error" << std::endl;
		exit(EXIT_FAILURE);
	}
	// set all zero
	std::fill_n((float*)features1[current_policy_value_queue_index][current_policy_value_batch_index], (int)ColorNum * MAX_FEATURES1_NUM * (int)SquareNum, 0.0f);
	std::fill_n((float*)features2[current_policy_value_queue_index][current_policy_value_batch_index], MAX_FEATURES2_NUM * (int)SquareNum, 0.0f);

	make_input_features(*pos, &features1[current_policy_value_queue_index][current_policy_value_batch_index], &features2[current_policy_value_queue_index][current_policy_value_batch_index]);
	policy_value_queue_node[current_policy_value_queue_index][current_policy_value_batch_index].node = &uct_node[index];
	policy_value_queue_node[current_policy_value_queue_index][current_policy_value_batch_index].color = color;
	current_policy_value_batch_index++;
	UNLOCK_QUEUE;
}

//////////////////////////
//  �T���ł��~�߂̊m�F  //
//////////////////////////
bool
UctSercher::InterruptionCheck(const unsigned int current_root, const int playout_count)
{
	int max = 0, second = 0;
	const int child_num = uct_node[current_root].child_num;
	const int rest = playout_num - playout_count;
	child_node_t *uct_child = uct_node[current_root].child;

	// �T���񐔂��ł�������Ǝ��ɑ���������߂�
	for (int i = 0; i < child_num; i++) {
		if (uct_child[i].move_count > max) {
			second = max;
			max = uct_child[i].move_count;
		}
		else if (uct_child[i].move_count > second) {
			second = uct_child[i].move_count;
		}
	}

	// �c��̒T����S�Ď��P��ɔ�₵�Ă�
	// �őP��𒴂����Ȃ��ꍇ�͒T����ł��؂�
	if (max - second > rest) {
		return true;
	}
	else {
		return false;
	}
}

// �ǖʂ̕]��
void EvalNode() {
	bool enough_batch_size = false;
	while (true) {
		LOCK_QUEUE;
		if (running_threads == 0 && current_policy_value_batch_index == 0) {
			UNLOCK_QUEUE;
			break;
		}

		if (current_policy_value_batch_index == 0) {
			UNLOCK_QUEUE;
			this_thread::sleep_for(chrono::milliseconds(1));
			//cerr << "EMPTY QUEUE" << endl;
			continue;
		}

		if (running_threads > 0 && (current_policy_value_batch_index == 0 || !enough_batch_size && current_policy_value_batch_index < running_threads * 0.9)) {
			UNLOCK_QUEUE;
			this_thread::sleep_for(chrono::milliseconds(1));
			enough_batch_size = true;
		}
		else {
			enough_batch_size = false;
			int policy_value_batch_size = current_policy_value_batch_index;
			int policy_value_queue_index = current_policy_value_queue_index;
			current_policy_value_batch_index = 0;
			current_policy_value_queue_index = current_policy_value_queue_index ^ 1;
			UNLOCK_QUEUE;
			SPDLOG_DEBUG(logger, "policy_value_batch_size:{}", policy_value_batch_size);

			// predict
			np::ndarray ndfeatures1 = np::from_data(
				features1[policy_value_queue_index],
				np::dtype::get_builtin<float>(),
				py::make_tuple(policy_value_batch_size, (int)ColorNum * MAX_FEATURES1_NUM, 9, 9),
				py::make_tuple(sizeof(float)*(int)ColorNum*MAX_FEATURES1_NUM * 81, sizeof(float) * 81, sizeof(float) * 9, sizeof(float)),
				py::object());

			np::ndarray ndfeatures2 = np::from_data(
				features2[policy_value_queue_index],
				np::dtype::get_builtin<float>(),
				py::make_tuple(policy_value_batch_size, MAX_FEATURES2_NUM, 9, 9),
				py::make_tuple(sizeof(float)*MAX_FEATURES2_NUM * 81, sizeof(float) * 81, sizeof(float) * 9, sizeof(float)),
				py::object());

			auto ret = dlshogi_predict(ndfeatures1, ndfeatures2);
			py::tuple ret_list = py::extract<py::tuple>(ret);
			np::ndarray y1_data = py::extract<np::ndarray>(ret_list[0]);
			np::ndarray y2_data = py::extract<np::ndarray>(ret_list[1]);

			float(*logits)[MAX_MOVE_LABEL_NUM * SquareNum] = reinterpret_cast<float(*)[MAX_MOVE_LABEL_NUM * SquareNum]>(y1_data.get_data());
			float *value = reinterpret_cast<float*>(y2_data.get_data());

			for (int i = 0; i < policy_value_batch_size; i++, logits++, value++) {
				policy_value_queue_node_t queue_node = policy_value_queue_node[policy_value_queue_index][i];

				/*if (index == current_root) {
				string str;
				for (int sq = 0; sq < SquareNum; sq++) {
				str += to_string((int)features1[policy_value_queue_index][i][0][0][sq]);
				str += " ";
				}
				cout << str << endl;
				}*/

				// �΋ǂ�1�X���b�h�ōs�����߃��b�N�͕s�v
				const int child_num = queue_node.node->child_num;
				child_node_t *uct_child = queue_node.node->child;
				Color color = queue_node.color;

				// ���@��ꗗ
				vector<float> legal_move_probabilities;
				for (int j = 0; j < child_num; j++) {
					Move move = uct_child[j].move;
					const int move_label = make_move_label((u16)move.proFromAndTo(), color);
					legal_move_probabilities.emplace_back((*logits)[move_label]);
				}

				// Boltzmann distribution
				softmax_tempature_with_normalize(legal_move_probabilities);

				for (int j = 0; j < child_num; j++) {
					uct_child[j].nnrate = legal_move_probabilities[j];
				}

				queue_node.node->value_win = *value;
				queue_node.node->evaled = true;
			}
		}
	}
}

// �A���Ŏ��ȑ΋ǂ���
void UctSercher::SelfPlay()
{
	int playout = 0;
	int ply = 0;
	GameResult gameResult;
	unsigned int current_root;

	std::unordered_set<Key> keyHash;
	StateListPtr states = nullptr;
	std::vector<HuffmanCodedPosAndEval> hcpevec;


	// �ǖʊǗ��ƒT���X���b�h
	Searcher s;
	s.init();
	const std::string options[] = {
		"name Threads value 1",
		"name MultiPV value 1",
		"name USI_Hash value 256",
		"name OwnBook value false",
		"name Max_Random_Score_Diff value 0" };
	for (auto& str : options) {
		std::istringstream is(str);
		s.setOption(is);
	}
	Position pos(DefaultStartPositionSFEN, s.threads.main(), s.thisptr);

#ifdef USE_MATE_ROOT_SEARCH
	s.tt.clear();
	s.threads.main()->previousScore = ScoreInfinite;
	LimitsType limits;
	limits.infinite = true;
#endif

	uniform_int_distribution<s64> inputFileDist(0, entryNum - 1);

	// �v���C�A�E�g���J��Ԃ�
	while (true) {
		// ��ԊJ�n
		if (playout == 0) {
			// �V�����Q�[���J�n
			if (ply == 0) {
				// �S�X���b�h�����������ǖʐ��������ǖʐ��ȏ�ɂȂ�����I��
				if (idx >= teacherNodes) {
					break;
				}

				ply = 1;

				// �J�n�ǖʂ��ǖʏW���烉���_���ɑI��
				HuffmanCodedPos hcp;
				{
					std::unique_lock<Mutex> lock(imutex);
					ifs.seekg(inputFileDist(mt) * sizeof(HuffmanCodedPos), std::ios_base::beg);
					ifs.read(reinterpret_cast<char*>(&hcp), sizeof(hcp));
				}
				setPosition(pos, hcp);
				randomMove(pos, mt); // ���t�ǖʂ𑝂₷�ׁA�擾�������ǖʂ��烉���_���ɓ������Ă����B
				SPDLOG_DEBUG(logger, "thread:{} ply:{} {}", threadID, ply, pos.toSFEN());

				keyHash.clear();
				states = StateListPtr(new std::deque<StateInfo>(1));
				hcpevec.clear();
			}

			// �n�b�V���N���A
			uct_hash.ClearUctHash();

			// ���[�g�m�[�h�W�J
			current_root = ExpandRoot(&pos);

			// �l�݂̃`�F�b�N
			if (uct_node[current_root].child_num == 0) {
				gameResult = (pos.turn() == Black) ? WhiteWin : BlackWin;
				goto L_END_GAME;
			}
			else if (uct_node[current_root].child_num == 1) {
				// 1�肵���Ȃ��Ƃ��́A���̎���w���Ď��̎�Ԃ�
				states->push_back(StateInfo());
				pos.doMove(uct_node[current_root].child[0].move, states->back());
				playout = 0;
				continue;
			}
			else if (uct_node[current_root].value_win == VALUE_WIN) {
				// �l��
				gameResult = (pos.turn() == Black) ? BlackWin : WhiteWin;
				goto L_END_GAME;
			}
			else if (uct_node[current_root].value_win == VALUE_LOSE) {
				// ���ʂ̋l��
				gameResult = (pos.turn() == Black) ? WhiteWin : BlackWin;
				goto L_END_GAME;
			}

#ifdef USE_MATE_ROOT_SEARCH
			// �l�ݒT���J�n
			pos.searcher()->alpha = -ScoreMaxEvaluate;
			pos.searcher()->beta = ScoreMaxEvaluate;
			pos.searcher()->threads.startThinking(pos, limits, pos.searcher()->states);
#endif
		}

		// �v���C�A�E�g
		UctSearch(&pos, current_root, 0);

		// �v���C�A�E�g�񐔉��Z
		playout++;

		// �T���I������
		if (InterruptionCheck(current_root, playout)) {
			// �T���񐔍ő�̎��������
			child_node_t* uct_child = uct_node[current_root].child;
			int max_count = 0;
			unsigned int select_index;
			for (int i = 0; i < uct_node[current_root].child_num; i++) {
				if (uct_child[i].move_count > max_count) {
					select_index = i;
					max_count = uct_child[i].move_count;
				}
				SPDLOG_DEBUG(logger, "thread:{} {}:{} move_count:{} win_rate:{}", threadID, i, uct_child[i].move.toUSI(), uct_child[i].move_count, uct_child[i].win / (uct_child[i].move_count + 0.0001f));
			}

			// �I����������̏����̎Z�o
			float best_wp = uct_child[select_index].win / uct_child[select_index].move_count;
			Move best_move = uct_child[select_index].move;
			SPDLOG_DEBUG(logger, "thread:{} bestmove:{} winrate:{}", threadID, best_move.toUSI(), best_wp);

#ifdef USE_MATE_ROOT_SEARCH
			{
				// �l�ݒT���I��
				pos.searcher()->signals.stop = true;
				pos.searcher()->threads.main()->waitForSearchFinished();
				Score score = pos.searcher()->threads.main()->rootMoves[0].score;
				const Move bestMove = pos.searcher()->threads.main()->rootMoves[0].pv[0];

				// �Q�[���I������
				// �����F�]���l��臒l�𒴂����ꍇ
				const int ScoreThresh = 3000; // ���ȑ΋ǂ������������Ƃ��Ď~�߂�臒l
				if (ScoreThresh < abs(score)) { // �����t�����̂œ����������ɂ���B
					if (pos.turn() == Black)
						gameResult = (score < ScoreZero ? WhiteWin : BlackWin);
					else
						gameResult = (score < ScoreZero ? BlackWin : WhiteWin);

					goto L_END_GAME;
				}
				else if (!bestMove) { // �����錾
					gameResult = (pos.turn() == Black ? BlackWin : WhiteWin);
					goto L_END_GAME;
				}
			}
#else
			{
				// ������臒l�𒴂����ꍇ�A�Q�[���I��
				const float winrate = (best_wp - 0.5f) * 2.0f;
				const float winrate_threshold = 0.99f;
				if (winrate_threshold < abs(winrate)) {
					if (pos.turn() == Black)
						gameResult = (winrate < 0 ? WhiteWin : BlackWin);
					else
						gameResult = (winrate < 0 ? BlackWin : WhiteWin);

					goto L_END_GAME;
				}
			}
#endif

			// �ǖʒǉ�
			hcpevec.emplace_back(HuffmanCodedPosAndEval());
			HuffmanCodedPosAndEval& hcpe = hcpevec.back();
			hcpe.hcp = pos.toHuffmanCodedPos();
			const Color rootTurn = pos.turn();
			hcpe.eval = s16(-logf(1.0f / best_wp - 1.0f) * 756.0864962951762f);
			hcpe.bestMove16 = static_cast<u16>(uct_child[select_index].move.value());

			// ���̎萔�ȏ�ň�������
			if (ply > 200) {
				gameResult = Draw;
				goto L_END_GAME;
			}

			// ����
			states->push_back(StateInfo());
			pos.doMove(best_move, states->back());

			// ���̎��
			playout = 0;
			ply++;
			SPDLOG_DEBUG(logger, "thread:{} ply:{} {}", threadID, ply, pos.toSFEN());
		}
		continue;

	L_END_GAME:
		SPDLOG_DEBUG(logger, "thread:{} ply:{} gameResult:{}", threadID, ply, gameResult);
		// ���������͏o�͂��Ȃ�
		if (gameResult != Draw) {
			// ���s��1�ǑS�Ăɕt����B
			for (auto& elem : hcpevec)
				elem.gameResult = gameResult;

			// �ǖʏo��
			if (hcpevec.size() > 0) {
				std::unique_lock<Mutex> lock(omutex);
				Position po;
				po.set(hcpevec[0].hcp, nullptr);
				ofs.write(reinterpret_cast<char*>(hcpevec.data()), sizeof(HuffmanCodedPosAndEval) * hcpevec.size());
			}
			idx++;
		}

		// �V�����Q�[��
		playout = 0;
		ply = 0;
	}
}

void SelfPlay(const int threadID)
{
	logger->info("selfplay thread:{}", threadID);
	UctSercher uct_sercher(threadID);
	uct_sercher.SelfPlay();
}

// ���t�ǖʐ���
void make_teacher(const char* recordFileName, const char* outputFileName)
{
	// �����ǖʏW
	ifs.open(recordFileName, ifstream::in | ifstream::binary | ios::ate);
	if (!ifs) {
		cerr << "Error: cannot open " << recordFileName << endl;
		exit(EXIT_FAILURE);
	}
	entryNum = ifs.tellg() / sizeof(HuffmanCodedPos);

	// ���t�ǖʂ�ۑ�����t�@�C��
	ofs.open(outputFileName, ios::binary);
	if (!ofs) {
		cerr << "Error: cannot open " << outputFileName << endl;
		exit(EXIT_FAILURE);
	}

	// ���f���ǂݍ���
	ReadWeights();

	// �X���b�h�쐬
	running_threads = threads;
	for (int i = 0; i < threads; i++) {
		handle[i] = new thread(SelfPlay, i);
	}

	// use_nn
	handle[threads] = new thread(EvalNode);

	for (int i = 0; i < threads; i++) {
		handle[i]->join();
		running_threads--;
		delete handle[i];
		handle[i] = nullptr;
	}
	// use_nn
	handle[threads]->join();
	delete handle[threads];
	handle[threads] = nullptr;

	ifs.close();
	ofs.close();
}

int main(int argc, char* argv[]) {
	if (argc < 8) {
		cout << "make_hcpe_by_self_play <eval_dir> <modelfile> roots.hcp output.teacher <threads> <nodes> <playout_num>" << endl;
		return 0;
	}

	char* evalDir = argv[1];
	model_path = argv[2];
	char* recordFileName = argv[3];
	char* outputFileName = argv[4];
	threads = stoi(argv[5]);
	teacherNodes = stoi(argv[6]);
	playout_num = stoi(argv[7]);

	if (teacherNodes <= 0)
		return 0;
	if (threads <= 0 || threads > policy_value_batch_maxsize)
		return 0;
	if (playout_num <= 0)
		return 0;

	logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
	logger->set_level(spdlog::level::trace);
	logger->info("{} {} {} {} {} {} {}", evalDir, model_path, recordFileName, outputFileName, threads, teacherNodes, playout_num);

	handle = new thread*[threads + 1];

	initTable();
	Position::initZobrist();
	HuffmanCodedPos::init();

#ifdef USE_MATE_ROOT_SEARCH
	logger->info("init evaluator");
	// �ꎞ�I�u�W�F�N�g�𐶐����� Evaluator::init() ���Ă񂾒���ɃI�u�W�F�N�g��j������B
	// �]���֐��̎��������������f�[�^���i�[���镪�̃����������ʂȈׁA
	std::unique_ptr<Evaluator>(new Evaluator)->init(evalDir, true);
#endif

	logger->info("make_teacher");
	make_teacher(recordFileName, outputFileName);

	spdlog::drop_all();
}
