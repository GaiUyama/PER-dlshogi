#pragma once

// ���l�߃`�F�b�N
// ��ԑ�������łȂ�����
bool mateMoveInOddPly(Position& pos, int depth);

// ������l�߃`�F�b�N
// ��ԑ������肳��Ă��邱��
bool mateMoveInEvenPly(Position& pos, int depth);

// 3��l�߃`�F�b�N
// ��ԑ�������łȂ�����
bool mateMoveIn3Ply(Position& pos);

// 2��l�߃`�F�b�N
// ��ԑ������肳��Ă��邱��
bool mateMoveIn2Ply(Position& pos);
