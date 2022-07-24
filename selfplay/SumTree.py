import numpy as np

class SumTree:
    # データを追加する見かけ上のインデックス
    write = 0

    # capacity: replay memoryの大きさ
    def __init__(self, capacity):
        self.capacity = capacity
        self.tree = np.zeros( 2*capacity - 1 )
        self.data = np.zeros( capacity, dtype=object )

    # change: 値の変化量
    def _propagate(self, idx, change):
        # parent: 親ノードのインデックス
        # //: 割り算の整数部（整数除算)
        parent = (idx - 1) // 2

        self.tree[parent] += change

        # 親ノードが根でない場合
        if parent != 0:
            # 親ノードの親ノードの値を変更
            self._propagate(parent, change)

    # 優先度の検索
    # s: 求めたい優先度
    def _retrieve(self, idx, s):
        # 左子ノードのインデックス
        left = 2 * idx + 1
        # 右子ノードのインデックス
        right = left + 1

        # 検索終了
        if left >= len(self.tree):
            return idx

        if s <= self.tree[left]:
            return self._retrieve(left, s)
        else:
            return self._retrieve(right, s-self.tree[left])

    # 優先度の合計=根
    def total(self):
        return self.tree[0]

    # p: 優先度
    def add(self, p, data):
        # データを追加するツリー内のインデックス
        idx = self.write + self.capacity - 1

        self.data[self.write] = data
        self.update(idx, p)

        self.write += 1
        if self.write >= self.capacity:
            self.write = 0

    def update(self, idx, p):
        # change: 値の変化量
        change = p - self.tree[idx]

        self.tree[idx] = p
        self._propagate(idx, change)

    def get(self, s):
        idx = self._retrieve(0, s)
        dataIdx = idx - self.capacity - 1 

        return (idx, self.tree[idx], self.data[dataIdx])
