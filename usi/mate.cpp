#include "position.hpp"
#include "move.hpp"
#include "generateMoves.hpp"

#include "mate.h"

// ���l�߃`�F�b�N
// ��ԑ�������łȂ�����
// �l�܂����Ԃ��o�[�W����
Move mateMoveInOddPlyReturnMove(Position& pos, int depth) {
	// OR�ߓ_

	// ���ׂĂ̍��@��ɂ���
	for (MoveList<Legal> ml(pos); !ml.end(); ++ml) {
		// 1�蓮����
		StateInfo state;
		pos.doMove(ml.move(), state);

		// ���肩�ǂ���
		if (pos.inCheck()) {
			//std::cout << ml.move().toUSI() << std::endl;
			// ����̏ꍇ
			// ������l�߃`�F�b�N
			if (mateMoveInEvenPly(pos, depth - 1)) {
				// �l�݂������������_�ŏI��
				pos.undoMove(ml.move());
				return ml.move();
			}
		}

		pos.undoMove(ml.move());
	}
	return Move::moveNone();
}

// ���l�߃`�F�b�N
// ��ԑ�������łȂ�����
bool mateMoveInOddPly(Position& pos, int depth)
{
	// OR�ߓ_

	// ���ׂĂ̍��@��ɂ���
	for (MoveList<Legal> ml(pos); !ml.end(); ++ml) {
		// 1�蓮����
		StateInfo state;
		pos.doMove(ml.move(), state);

		// ���肩�ǂ���
		if (pos.inCheck()) {
			//std::cout << ml.move().toUSI() << std::endl;
			// ����̏ꍇ
			// ������l�߃`�F�b�N
			if (mateMoveInEvenPly(pos, depth - 1)) {
				// �l�݂������������_�ŏI��
				pos.undoMove(ml.move());
				return true;
			}
		}

		pos.undoMove(ml.move());
	}
	return false;
}

// ������l�߃`�F�b�N
// ��ԑ������肳��Ă��邱��
bool mateMoveInEvenPly(Position& pos, int depth)
{
	// AND�ߓ_

	// ���ׂĂ�Evasion�ɂ���
	for (MoveList<Legal> ml(pos); !ml.end(); ++ml) {
		//std::cout << " " << ml.move().toUSI() << std::endl;
		// 1�蓮����
		StateInfo state;
		pos.doMove(ml.move(), state);

		// ���肩�ǂ���
		if (pos.inCheck()) {
			// ����̏ꍇ
			// �l�݂�������Ȃ��������_�ŏI��
			pos.undoMove(ml.move());
			return false;
		}

		if (depth == 4) {
			// 3��l�߂��ǂ���
			if (!mateMoveIn3Ply(pos)) {
				// 3��l�߂łȂ��ꍇ
				// �l�݂�������Ȃ��������_�ŏI��
				pos.undoMove(ml.move());
				return false;
			}
		}
		else {
			// ���l�߂��ǂ���
			if (!mateMoveInOddPly(pos, depth - 1)) {
				// ������l�߂łȂ��ꍇ
				// �l�݂�������Ȃ��������_�ŏI��
				pos.undoMove(ml.move());
				return false;
			}
		}

		pos.undoMove(ml.move());
	}
	return true;
}

// 3��l�߃`�F�b�N
// ��ԑ�������łȂ�����
bool mateMoveIn3Ply(Position& pos)
{
	// OR�ߓ_

	// ���ׂĂ̍��@��ɂ���
	for (MoveList<Legal> ml(pos); !ml.end(); ++ml) {
		// 1�蓮����
		StateInfo state;
		pos.doMove(ml.move(), state);

		// ���肩�ǂ���
		if (pos.inCheck()) {
			//std::cout << ml.move().toUSI() << std::endl;
			// ����̏ꍇ
			// 2��l�߃`�F�b�N
			if (mateMoveIn2Ply(pos)) {
				// �l�݂������������_�ŏI��
				pos.undoMove(ml.move());
				return true;
			}
		}

		pos.undoMove(ml.move());
	}
	return false;
}

// 2��l�߃`�F�b�N
// ��ԑ������肳��Ă��邱��
bool mateMoveIn2Ply(Position& pos)
{
	// AND�ߓ_

	// ���ׂĂ�Evasion�ɂ���
	for (MoveList<Legal> ml(pos); !ml.end(); ++ml) {
		//std::cout << " " << ml.move().toUSI() << std::endl;
		// 1�蓮����
		StateInfo state;
		pos.doMove(ml.move(), state);

		// ���肩�ǂ���
		if (pos.inCheck()) {
			// ����̏ꍇ
			// �l�݂�������Ȃ��������_�ŏI��
			pos.undoMove(ml.move());
			return false;
		}

		// 1��l�߂��ǂ���
		if (pos.mateMoveIn1Ply() == Move::moveNone()) {
			// 1��l�߂łȂ��ꍇ
			// �l�݂�������Ȃ��������_�ŏI��
			pos.undoMove(ml.move());
			return false;
		}

		pos.undoMove(ml.move());
	}
	return true;
}