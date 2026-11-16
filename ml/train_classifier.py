#!/usr/bin/env python3
"""
Alaga Alerta — treina o classificador de risco de alagamento (árvore de decisão) e exporta
para C (wokwi/classifier.h).

Entrada do modelo: janela de WINDOW leituras de NÍVEL DA ÁGUA em % da altura do bueiro
(0 = vazio, 100 = transbordando), uma leitura a cada SAMPLE_MS.
Classes: NORMAL, ATENCAO, PERIGO (mesmas do wokwi/sketch.ino).

ATENÇÃO: os dados são SINTÉTICOS e os rótulos vêm de regras de projeto (hipóteses, não norma
técnica). A métrica mede o quanto a árvore aprendeu essas regras, NÃO o desempenho em campo.
O gás (MQ-2) e o DHT não entram no modelo: no sketch o gás só sobe NORMAL para ATENCAO.

Uso:  python ml/train_classifier.py
Saída: wokwi/classifier.h, docs/metricas_classificador.txt, ml/tests/parity_vectors.txt
"""
import os
import numpy as np
from sklearn.tree import DecisionTreeClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report, confusion_matrix

SEED = 42
WINDOW = 10            # = WINDOW_SIZE do sketch
SAMPLE_MS = 500        # = SAMPLE_INTERVAL_MS do sketch
VAL_MIN, VAL_MAX = 0.0, 100.0   # nível em %
CLASSES = ["NORMAL", "ATENCAO", "PERIGO"]
N_PER_CLASS = 3000

rng = np.random.default_rng(SEED)


def extract_features(w):
    """IDÊNTICO a extractFeatures() em wokwi/features.h e wokwi/sketch.ino (float32)."""
    w = np.clip(np.nan_to_num(np.asarray(w, dtype=np.float32), nan=VAL_MIN), VAL_MIN, VAL_MAX)
    n = len(w)
    mean = np.float32(0)
    for v in w:
        mean = np.float32(mean + v)
    mean = np.float32(mean / np.float32(n))
    mid = np.float32((n - 1) / 2.0)
    num = np.float32(0)
    den = np.float32(0)
    for i in range(n):
        di = np.float32(np.float32(i) - mid)
        num = np.float32(num + di * np.float32(w[i] - mean))
        den = np.float32(den + di * di)
    slope = np.float32(num / den)                                   # % por amostra
    rate = np.float32(slope * np.float32(np.float32(60000.0) / np.float32(SAMPLE_MS)))  # % por min
    return np.array([mean, rate, w.max()], dtype=np.float32)


def label(n, r):
    """Regras de rotulagem (hipóteses de projeto). n = nível médio (%), r = taxa (%/min)."""
    if n >= 70 or (n >= 45 and r >= 8) or (n >= 30 and r >= 16):
        return 2  # PERIGO
    if n >= 40 or (n >= 20 and r >= 4) or (n >= 10 and r >= 10):
        return 1  # ATENCAO
    return 0      # NORMAL


def random_window(gen):
    """Janela sintética: nível médio e taxa sorteados, reta + ruído de leitura, limitada a 0..100."""
    n = gen.uniform(0, 100)
    r = gen.choice([gen.uniform(-15, 5), gen.uniform(0, 30), gen.uniform(0, 60)])  # %/min
    slope = r * SAMPLE_MS / 60000.0                       # % por amostra
    x = np.arange(WINDOW) - (WINDOW - 1) / 2.0
    w = n + slope * x + gen.normal(0, gen.uniform(0.2, 1.5), WINDOW)
    return np.clip(w, VAL_MIN, VAL_MAX).astype(np.float32)


def gen_window(cls, gen=None):
    """Janela da classe `cls`: sorteia até a regra de rotulagem (sobre os atributos) dar `cls`."""
    gen = gen or rng
    while True:
        w = random_window(gen)
        f = extract_features(w)
        if label(float(f[0]), float(f[1])) == cls:
            return w


def build_dataset():
    X, y = [], []
    for c in range(len(CLASSES)):
        for _ in range(N_PER_CLASS):
            X.append(extract_features(gen_window(c)))
            y.append(c)
    return np.array(X), np.array(y)


def tree_to_c(clf, name="classifyWindow"):
    t = clf.tree_
    lines = []

    def rec(node, depth):
        pad = "  " * depth
        if t.children_left[node] == -1:
            cls = int(np.argmax(t.value[node][0]))
            lines.append(f"{pad}return {cls};  // {CLASSES[cls]}")
            return
        f, thr = int(t.feature[node]), float(t.threshold[node])
        lines.append(f"{pad}if (f[{f}] <= {thr:.6f}f) {{")
        rec(t.children_left[node], depth + 1)
        lines.append(f"{pad}}} else {{")
        rec(t.children_right[node], depth + 1)
        lines.append(f"{pad}}}")

    rec(0, 1)
    body = "\n".join(lines)
    defs = "\n".join(f"#define CLASS_{c:<8} {i}" for i, c in enumerate(CLASSES))
    names = ", ".join(f'"{c}"' for c in CLASSES)
    return f"""// classifier.h — GERADO por ml/train_classifier.py (não editar à mão).
// Atributos: f[0] nível médio (%), f[1] taxa de subida (%/min), f[2] nível máximo (%).
#pragma once
{defs}

static const char *CLASS_NAMES[{len(CLASSES)}] = {{{names}}};

static inline int {name}(const float *f) {{
{body}
}}
"""


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    X, y = build_dataset()
    Xtr, Xte, ytr, yte = train_test_split(X, y, test_size=0.25, random_state=SEED, stratify=y)
    clf = DecisionTreeClassifier(max_depth=5, min_samples_leaf=20, random_state=SEED)
    clf.fit(Xtr, ytr)
    pred = clf.predict(Xte)

    with open(os.path.join(root, "wokwi", "classifier.h"), "w", encoding="utf-8") as fh:
        fh.write(tree_to_c(clf))

    out = (f"MÉTRICAS DO CLASSIFICADOR (dados SINTÉTICOS — não medem desempenho em campo)\n"
           f"Janela={WINDOW} leituras a cada {SAMPLE_MS} ms | treino={len(ytr)} | teste={len(yte)} "
           f"| nós={clf.tree_.node_count}\n\n"
           + classification_report(yte, pred, target_names=CLASSES, digits=3)
           + "\nMatriz de confusão (linhas=real, colunas=previsto)\n" + str(confusion_matrix(yte, pred)) + "\n")
    with open(os.path.join(root, "docs", "metricas_classificador.txt"), "w", encoding="utf-8") as fh:
        fh.write(out)
    print(out)

    # vetores de paridade para ml/tests/host_test.cpp
    rng2 = np.random.default_rng(7)
    with open(os.path.join(root, "ml", "tests", "parity_vectors.txt"), "w") as fh:
        for _ in range(300):
            w = gen_window(int(rng2.integers(len(CLASSES))), rng2).astype(np.float32)
            p = int(clf.predict([extract_features(w)])[0])
            fh.write(" ".join(f"{v:.6f}" for v in w) + f" {p}\n")


if __name__ == "__main__":
    main()
