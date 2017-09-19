#include "position.hpp"
#include "move.hpp"
#include "generateMoves.hpp"

#include "mate.h"

// 7��l�߃`�F�b�N
// ��ԑ�������łȂ�����
bool mateMoveIn7Ply(Position& pos)
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
			// 6��l�߃`�F�b�N
			if (mateMoveIn6Ply(pos)) {
				// �l�݂������������_�ŏI��
				pos.undoMove(ml.move());
				return true;
			}
		}

		pos.undoMove(ml.move());
	}
	return false;
}

// 6��l�߃`�F�b�N
// ��ԑ������肳��Ă��邱��
bool mateMoveIn6Ply(Position& pos)
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

		// 5��l�߂��ǂ���
		if (!mateMoveIn5Ply(pos)) {
			// 5��l�߂łȂ��ꍇ
			// �l�݂�������Ȃ��������_�ŏI��
			pos.undoMove(ml.move());
			return false;
		}

		pos.undoMove(ml.move());
	}
	return true;
}

// 5��l�߃`�F�b�N
// ��ԑ�������łȂ�����
bool mateMoveIn5Ply(Position& pos)
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
			// 4��l�߃`�F�b�N
			if (mateMoveIn4Ply(pos)) {
				// �l�݂������������_�ŏI��
				pos.undoMove(ml.move());
				return true;
			}
		}

		pos.undoMove(ml.move());
	}
	return false;
}

// 4��l�߃`�F�b�N
// ��ԑ������肳��Ă��邱��
bool mateMoveIn4Ply(Position& pos)
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

		// 3��l�߂��ǂ���
		if (!mateMoveIn3Ply(pos)) {
			// 3��l�߂łȂ��ꍇ
			// �l�݂�������Ȃ��������_�ŏI��
			pos.undoMove(ml.move());
			return false;
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