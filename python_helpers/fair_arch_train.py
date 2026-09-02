"""Train fairness-constrained MLPs with a user-specified architecture.

The architecture is given exactly the way it is written in PyTorch, as a list
of ``nn`` modules, e.g.::

    layers = [
        nn.Linear(input_size, 2),
        nn.ReLU(),
        nn.Linear(2, 4),
        nn.ReLU(),
        nn.Linear(4, 2)
    ]

Pass it either inline (``--arch "[nn.Linear(input_size, 2), ...]"``) or in a
small python file (``--arch-file my_arch.py``) that binds ``layers``.  The name
``input_size`` is pre-bound to the feature count of the chosen dataset, so the
same architecture string works across datasets.

Datasets and their protected attributes:

    adult   -> sex             (UCI Adult Income)
    credit  -> SEX             (UCI Default of Credit Card Clients)
    german  -> foreign_worker  (UCI Statlog German Credit)

Fairness is enforced during training by one of:

    --method none  plain ERM baseline (for comparison)
    --method dp    differentiable demographic-parity penalty
    --method eo    differentiable equalized-odds penalty
    --method adv   adversarial debiasing (gradient reversal on an adversary
                   that tries to recover the protected attribute from the
                   model's logits)

Examples::

    python fair_arch_train.py --dataset adult --method dp --lambda-fair 1.0
    python fair_arch_train.py --dataset german --arch-file arch.py --method adv
    python fair_arch_train.py --dataset all --method eo --onnx-dir eran_models
"""

import sys as _sys, os as _os
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
from paths import project_path
import argparse
import os
import sys

import numpy as np
import pandas as pd
import torch
import torch.nn as nn
import torch.optim as optim
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import LabelEncoder, StandardScaler
from torch.utils.data import DataLoader, TensorDataset

# ==================== Dataset loading ====================
#
# Every loader returns (X, y, s, feature_names):
#   X  float32 [n, d] features
#   y  int64   [n]    binary label (1 = "positive"/favourable outcome class)
#   s  int64   [n]    binary protected attribute (1 = privileged group)

ADULT_URL = "https://archive.ics.uci.edu/ml/machine-learning-databases/adult/adult.data"
CREDIT_URL = ("https://archive.ics.uci.edu/ml/machine-learning-databases/00350/"
              "default%20of%20credit%20card%20clients.xls")
GERMAN_URL = "https://archive.ics.uci.edu/ml/machine-learning-databases/statlog/german/german.data"

ADULT_COLS = ['age', 'workclass', 'fnlwgt', 'education', 'education-num',
              'marital-status', 'occupation', 'relationship', 'race', 'sex',
              'capital-gain', 'capital-loss', 'hours-per-week', 'native-country',
              'income']

GERMAN_COLS = ['checking_status', 'duration', 'credit_history', 'purpose',
               'credit_amount', 'savings_status', 'employment',
               'installment_rate', 'personal_status_sex', 'other_debtors',
               'residence_since', 'property', 'age', 'other_installment_plans',
               'housing', 'existing_credits', 'job', 'num_dependents',
               'telephone', 'foreign_worker', 'credit_risk']


def _encode_categoricals(df, skip):
    for col in df.columns:
        if col in skip:
            continue
        if df[col].dtype == 'object':
            df[col] = LabelEncoder().fit_transform(df[col].astype(str))
    return df


def load_adult(filepath=project_path('datasets/adult.csv'), drop_sensitive=False):
    """Adult Income.  Label: income > 50K.  Protected: sex (1 = Male)."""
    if os.path.exists(filepath):
        df = pd.read_csv(filepath, na_values=' ?', skipinitialspace=True)
    else:
        print(f"Downloading Adult dataset -> {filepath}")
        df = pd.read_csv(ADULT_URL, header=None, na_values=' ?', skipinitialspace=True)
        df.columns = ADULT_COLS
        df.to_csv(filepath, index=False)

    df = df.dropna()
    if 'sex' not in df.columns:
        raise KeyError(f"'sex' column missing from {filepath}; columns are {list(df.columns)}")

    y = df['income'].apply(lambda v: 1 if '>50K' in str(v) else 0).values.astype(np.int64)
    s = df['sex'].astype(str).str.strip().str.lower().apply(
        lambda v: 1 if v.startswith('m') else 0).values.astype(np.int64)

    feats = df.drop(columns=['income'])
    if drop_sensitive:
        feats = feats.drop(columns=['sex'])
    feats = _encode_categoricals(feats, skip=())
    return feats.values.astype(np.float32), y, s, feats.columns.tolist()


def load_credit(filepath=project_path('datasets/default_credit.csv'), drop_sensitive=False):
    """Default of Credit Card Clients.  Label: 1 = no default (favourable).

    Protected: SEX (1 = male in the raw encoding, 2 = female)."""
    if os.path.exists(filepath):
        df = pd.read_csv(filepath)
    else:
        print(f"Downloading Default Credit dataset -> {filepath}")
        df = pd.read_excel(CREDIT_URL, header=1)
        df.to_csv(filepath, index=False)

    df = df.dropna()
    df = df.drop(columns=[c for c in ('ID', 'Unnamed: 0') if c in df.columns])

    target_col = ('default payment next month'
                  if 'default payment next month' in df.columns else df.columns[-1])
    sens_col = next((c for c in df.columns if c.strip().lower() == 'sex'), None)
    if sens_col is None:
        raise KeyError(f"'SEX' column missing from {filepath}; columns are {list(df.columns)}")

    # Flip so that 1 is the favourable outcome, matching the other datasets.
    y = (1 - df[target_col].astype(int)).values.astype(np.int64)
    s = (df[sens_col].astype(int) == 1).astype(np.int64).values

    feats = df.drop(columns=[target_col])
    if drop_sensitive:
        feats = feats.drop(columns=[sens_col])
    feats = _encode_categoricals(feats, skip=())
    return feats.values.astype(np.float32), y, s, feats.columns.tolist()


def load_german(filepath=project_path('datasets/german_credit.csv'), drop_sensitive=False):
    """Statlog German Credit.  Label: 1 = good credit risk.

    Protected: foreign_worker (A201 = yes, A202 = no; 1 = non-foreign, i.e.
    the privileged group)."""
    if os.path.exists(filepath):
        df = pd.read_csv(filepath)
    else:
        print(f"Downloading German Credit dataset -> {filepath}")
        df = pd.read_csv(GERMAN_URL, sep=' ', header=None)
        df.columns = GERMAN_COLS
        df.to_csv(filepath, index=False)

    df = df.dropna()
    if 'foreign_worker' not in df.columns:
        # Tolerate an older attr1..attr20 dump written by fairness_train.py.
        if 'attr20' in df.columns:
            df = df.rename(columns=dict(zip([f'attr{i}' for i in range(1, 21)],
                                            GERMAN_COLS[:20])))
        else:
            raise KeyError(f"'foreign_worker' column missing from {filepath}; "
                           f"columns are {list(df.columns)}")

    y = (df['credit_risk'].astype(int) == 1).astype(np.int64).values  # 1 = good
    s = df['foreign_worker'].astype(str).str.strip().apply(
        lambda v: 0 if v == 'A201' else 1).values.astype(np.int64)

    feats = df.drop(columns=['credit_risk'])
    if drop_sensitive:
        feats = feats.drop(columns=['foreign_worker'])
    feats = _encode_categoricals(feats, skip=())
    return feats.values.astype(np.float32), y, s, feats.columns.tolist()


DATASETS = {
    'adult':  dict(loader=load_adult,  default_path=project_path('datasets/adult.csv'),          attr='sex'),
    'credit': dict(loader=load_credit, default_path=project_path('datasets/default_credit.csv'), attr='SEX'),
    'german': dict(loader=load_german, default_path=project_path('datasets/german_credit.csv'),  attr='foreign_worker'),
}

# ==================== Architecture specification ====================

DEFAULT_ARCH = """
layers = [
    nn.Linear(input_size, 50),
    nn.ReLU(),
    nn.Linear(50, 50),
    nn.ReLU(),
    nn.Linear(50, 2)
]
"""


def build_layers(arch_src, input_size, num_classes=2):
    """Evaluate an architecture spec into a list of nn.Modules.

    ``arch_src`` is python source that either binds ``layers`` or is a bare
    list expression.  ``input_size``, ``num_classes``, ``nn`` and ``torch`` are
    in scope."""
    scope = {'nn': nn, 'torch': torch, 'input_size': input_size,
             'num_classes': num_classes}
    src = arch_src.strip()
    try:
        layers = eval(src, scope)          # bare list expression
    except SyntaxError:
        exec(src, scope)                   # statement form: layers = [...]
        layers = scope.get('layers')

    if layers is None:
        raise ValueError("architecture spec did not define `layers`")
    layers = list(layers)
    if not all(isinstance(m, nn.Module) for m in layers):
        raise TypeError("every entry of `layers` must be an nn.Module")
    return layers


class ArchNet(nn.Module):
    def __init__(self, layers, init='he'):
        super().__init__()
        self.model = nn.Sequential(*layers)
        if init == 'he':
            # Narrow ReLU stacks (a 2-unit bottleneck, say) lose activation
            # scale under PyTorch's default Linear init and collapse to a
            # constant predictor.  He init with a small positive bias keeps
            # units alive without touching the exported operation sequence.
            for m in self.model:
                if isinstance(m, nn.Linear):
                    nn.init.kaiming_normal_(m.weight, nonlinearity='relu')
                    nn.init.constant_(m.bias, 0.01)

    def forward(self, x):
        return self.model(x)


class GradReverse(torch.autograd.Function):
    @staticmethod
    def forward(ctx, x, lambd):
        ctx.lambd = lambd
        return x.view_as(x)

    @staticmethod
    def backward(ctx, grad_out):
        return -ctx.lambd * grad_out, None


class Adversary(nn.Module):
    """Predicts the protected attribute from the classifier's logits."""

    def __init__(self, in_dim, hidden=32):
        super().__init__()
        self.net = nn.Sequential(nn.Linear(in_dim, hidden), nn.ReLU(),
                                 nn.Linear(hidden, hidden), nn.ReLU(),
                                 nn.Linear(hidden, 2))

    def forward(self, logits, lambd):
        return self.net(GradReverse.apply(logits, lambd))

# ==================== Fairness losses and metrics ====================


def _group_mean(values, mask):
    """Mean of `values` over `mask`, or None if the group is empty."""
    if mask.sum() == 0:
        return None
    return values[mask].mean()


def dp_penalty(probs, s):
    """|E[p(y=1)|s=1] - E[p(y=1)|s=0]| -- demographic parity gap, differentiable."""
    p1 = probs[:, 1]
    a = _group_mean(p1, s == 1)
    b = _group_mean(p1, s == 0)
    if a is None or b is None:
        return probs.sum() * 0.0
    return (a - b).abs()


def eo_penalty(probs, y, s):
    """Equalized-odds gap: TPR gap + FPR gap, both differentiable."""
    p1 = probs[:, 1]
    total = probs.sum() * 0.0
    for label in (0, 1):
        a = _group_mean(p1, (s == 1) & (y == label))
        b = _group_mean(p1, (s == 0) & (y == label))
        if a is not None and b is not None:
            total = total + (a - b).abs()
    return total


def fairness_metrics(y_true, y_pred, s):
    """Hard-prediction fairness report for the protected attribute."""
    y_true, y_pred, s = map(np.asarray, (y_true, y_pred, s))
    out = {}

    def rate(mask):
        return float(y_pred[mask].mean()) if mask.sum() else float('nan')

    pr1, pr0 = rate(s == 1), rate(s == 0)
    out['selection_rate_privileged'] = pr1
    out['selection_rate_unprivileged'] = pr0
    out['demographic_parity_diff'] = abs(pr1 - pr0)
    out['disparate_impact'] = (min(pr1, pr0) / max(pr1, pr0)
                               if max(pr1, pr0) > 0 else float('nan'))

    tpr1, tpr0 = rate((s == 1) & (y_true == 1)), rate((s == 0) & (y_true == 1))
    fpr1, fpr0 = rate((s == 1) & (y_true == 0)), rate((s == 0) & (y_true == 0))
    out['equal_opportunity_diff'] = abs(tpr1 - tpr0)
    out['equalized_odds_diff'] = abs(tpr1 - tpr0) + abs(fpr1 - fpr0)

    def acc(mask):
        return float((y_pred[mask] == y_true[mask]).mean()) if mask.sum() else float('nan')

    out['accuracy_privileged'] = acc(s == 1)
    out['accuracy_unprivileged'] = acc(s == 0)
    out['accuracy_gap'] = abs(out['accuracy_privileged'] - out['accuracy_unprivileged'])

    # Balanced accuracy: mean of per-class recall.  A constant predictor scores
    # 0.5 here no matter how skewed the labels are, which is what stops model
    # selection from crowning the trivial "always predict the majority" net.
    recalls = [float((y_pred[y_true == c] == c).mean())
               for c in (0, 1) if (y_true == c).sum()]
    out['balanced_accuracy'] = float(np.mean(recalls)) if recalls else float('nan')
    return out


def print_metrics(tag, acc, m):
    print(f"\n--- {tag} ---")
    print(f"  accuracy                 : {acc*100:.2f}%")
    print(f"  balanced accuracy        : {m['balanced_accuracy']*100:.2f}%   "
          f"(50% = constant predictor)")
    print(f"  demographic parity diff  : {m['demographic_parity_diff']:.4f}  "
          f"(rates {m['selection_rate_unprivileged']:.3f} / {m['selection_rate_privileged']:.3f})")
    print(f"  disparate impact ratio   : {m['disparate_impact']:.4f}   (1.0 is parity)")
    print(f"  equal opportunity diff   : {m['equal_opportunity_diff']:.4f}")
    print(f"  equalized odds diff      : {m['equalized_odds_diff']:.4f}")
    print(f"  accuracy gap (grp)       : {m['accuracy_gap']:.4f}")

# ==================== Training ====================


def run_epoch(model, adversary, loader, criterion, optimizer, adv_optimizer,
              args, device, train=True):
    model.train(train)
    totals = dict(loss=0.0, task=0.0, fair=0.0, correct=0, n=0)
    preds_all, y_all, s_all = [], [], []

    for xb, yb, sb in loader:
        xb, yb, sb = xb.to(device), yb.to(device), sb.to(device)
        with torch.set_grad_enabled(train):
            logits = model(xb)
            task_loss = criterion(logits, yb)
            probs = torch.softmax(logits, dim=1)

            if args.method == 'dp':
                fair_loss = dp_penalty(probs, sb)
            elif args.method == 'eo':
                fair_loss = eo_penalty(probs, yb, sb)
            elif args.method == 'adv':
                fair_loss = nn.functional.cross_entropy(
                    adversary(logits, args.lambda_fair), sb)
            else:
                fair_loss = torch.zeros((), device=device)

            # For 'adv' the gradient-reversal layer already carries the
            # lambda scaling into the encoder, so the adversary's own loss
            # enters the objective unscaled.
            loss = task_loss + (fair_loss if args.method == 'adv'
                                else args.lambda_fair * fair_loss)

        if train:
            optimizer.zero_grad()
            if adv_optimizer is not None:
                adv_optimizer.zero_grad()
            loss.backward()
            optimizer.step()
            if adv_optimizer is not None:
                adv_optimizer.step()

        pred = logits.argmax(1)
        bs = yb.size(0)
        totals['loss'] += loss.item() * bs
        totals['task'] += task_loss.item() * bs
        totals['fair'] += fair_loss.detach().item() * bs
        totals['correct'] += (pred == yb).sum().item()
        totals['n'] += bs
        preds_all.append(pred.cpu().numpy())
        y_all.append(yb.cpu().numpy())
        s_all.append(sb.cpu().numpy())

    n = max(totals['n'], 1)
    stats = dict(loss=totals['loss'] / n, task=totals['task'] / n,
                 fair=totals['fair'] / n, acc=totals['correct'] / n)
    return stats, (np.concatenate(y_all), np.concatenate(preds_all),
                   np.concatenate(s_all))


def make_loaders(X, y, s, args):
    # Stratify on the (label, group) cell, not the label alone.  German has
    # only 37 non-foreign workers; label-only stratification lets a split end
    # up with a handful of them, and the fairness gap measured on that split
    # is then noise.  Fall back to label-only if a joint cell is too small.
    joint = y.astype(np.int64) * 2 + s.astype(np.int64)
    strat = joint if np.bincount(joint).min() >= 2 else y

    Xtr, Xte, ytr, yte, str_, ste, jtr, _ = train_test_split(
        X, y, s, strat, test_size=args.test_size, random_state=args.seed,
        stratify=strat)
    strat_tr = jtr if np.bincount(jtr).min() >= 2 else ytr
    Xtr, Xva, ytr, yva, str_, sva = train_test_split(
        Xtr, ytr, str_, test_size=0.2, random_state=args.seed, stratify=strat_tr)

    scaler = StandardScaler()
    Xtr = scaler.fit_transform(Xtr)
    Xva = scaler.transform(Xva)
    Xte = scaler.transform(Xte)

    def ds(Xp, yp, sp):
        return TensorDataset(torch.FloatTensor(Xp), torch.LongTensor(yp),
                             torch.LongTensor(sp))

    tr = DataLoader(ds(Xtr, ytr, str_), batch_size=args.batch_size, shuffle=True)
    va = DataLoader(ds(Xva, yva, sva), batch_size=args.batch_size, shuffle=False)
    te = DataLoader(ds(Xte, yte, ste), batch_size=args.batch_size, shuffle=False)

    # The exported .txt holds the data in the form the network actually sees:
    # standardized with the train-fitted scaler.  `splits` lets the caller dump
    # the whole set or just the test split.
    splits = {'all': (np.vstack([Xtr, Xva, Xte]), np.concatenate([ytr, yva, yte])),
              'train': (Xtr, ytr), 'val': (Xva, yva), 'test': (Xte, yte)}
    return tr, va, te, scaler, splits




def sensitive_index(feature_names, attr):
    """0-based column of the protected attribute, or None if it was dropped."""
    lowered = [c.strip().lower() for c in feature_names]
    target = attr.strip().lower()
    return lowered.index(target) if target in lowered else None


def save_scaler_txt(scaler, feature_names, out_path):
    """Persist the standardization so raw records can be transformed later.

    Line 1: per-column mean.  Line 2: per-column scale (std).  Line 3: the
    column names.  Inference on a raw row is (raw - mean) / scale."""
    os.makedirs(os.path.dirname(out_path) or '.', exist_ok=True)
    with open(out_path, 'w') as f:
        f.write(' '.join('%.10g' % v for v in scaler.mean_) + '\n')
        f.write(' '.join('%.10g' % v for v in scaler.scale_) + '\n')
        f.write(' '.join(str(c) for c in feature_names) + '\n')
    print(f"  saved {out_path}")


def sensitive_encoding(X_raw, scaler, idx, attr):
    """Map each raw value of the protected column to its standardized value.

    The column is categorical, so it takes a handful of distinct raw codes;
    this is exactly what must be passed at that index during inference."""
    if idx is None:
        return None
    raw_vals = np.unique(X_raw[:, idx])
    mean, scale = float(scaler.mean_[idx]), float(scaler.scale_[idx])
    return {'attribute': attr, 'index': int(idx), 'mean': mean, 'scale': scale,
            'values': [(float(v), (float(v) - mean) / scale) for v in raw_vals]}


def print_sensitive_encoding(enc, n_features):
    if enc is None:
        print("\n  protected attribute was dropped from the features "
              "(--drop-sensitive); there is no index to set at inference.")
        return
    print(f"\n  INFERENCE: protected attribute '{enc['attribute']}' is input index "
          f"{enc['index']} of {n_features} (0-indexed).")
    print(f"    standardization for that column: mean {enc['mean']:.6g}, "
          f"scale {enc['scale']:.6g}")
    for raw, std in enc['values']:
        print(f"    raw {raw:>8.6g}  ->  pass {std:+.8g}")


def load_dataset_txt(path):
    """Inverse of save_dataset_txt: returns (X, y)."""
    labels, rows = [], []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if not parts:
                continue
            labels.append(int(parts[0]))
            rows.append([float(v) for v in parts[1:]])
    return np.asarray(rows, dtype=np.float32), np.asarray(labels, dtype=np.int64)


def verify_txt_matches_model(txt_path, model, expected_tail, device):
    """Read the written file back and confirm it drives the model identically.

    `expected_tail` is (n_rows, accuracy) for the trailing rows of the file --
    the test split -- as measured during training.  A mismatch means the text
    dump and the exported network disagree."""
    Xr, yr = load_dataset_txt(txt_path)
    n_test, expected_acc = expected_tail
    model.eval()
    with torch.no_grad():
        pred = model(torch.from_numpy(Xr).to(device)).argmax(1).cpu().numpy()
    tail_acc = float((pred[-n_test:] == yr[-n_test:]).mean())
    ok = abs(tail_acc - expected_acc) < 1e-6
    print(f"  read-back check: {txt_path} -> {Xr.shape[0]} rows x {Xr.shape[1]} attrs, "
          f"test-split accuracy {tail_acc*100:.2f}% vs {expected_acc*100:.2f}% in training "
          f"({'MATCH' if ok else 'MISMATCH'})")
    if not ok:
        raise RuntimeError(f"{txt_path} does not reproduce the trained model's predictions")
    return ok


def hidden_sizes(layers):
    """Out-features of every Linear except the final one -- the hidden widths.

    For [Linear(d,2), ReLU, Linear(2,4), ReLU, Linear(4,2)] this is [2, 4],
    which names the ONNX file `<dataset>_2_4.onnx`."""
    linears = [m for m in layers if isinstance(m, nn.Linear)]
    return [m.out_features for m in linears[:-1]]


def onnx_name(dataset, layers):
    widths = hidden_sizes(layers)
    suffix = '_'.join(str(w) for w in widths) if widths else 'linear'
    return f"{dataset}_{suffix}.onnx"


def save_dataset_txt(X, y, out_path, fmt='%.8g'):
    """One record per line: `<ground truth> <attr> <attr> ...`."""
    os.makedirs(os.path.dirname(out_path) or '.', exist_ok=True)
    with open(out_path, 'w') as f:
        for xi, yi in zip(X, y):
            f.write(str(int(yi)))
            for v in xi:
                f.write(' ' + (fmt % float(v)))
            f.write('\n')
    print(f"  saved {out_path}  ({len(y)} records, {X.shape[1]} attributes)")


def selection_score(stats, metrics, args):
    """Model-selection objective: utility traded off against the fairness gap.

    Utility is *balanced* accuracy, so a degenerate constant predictor scores
    0.5 with a 0.0 gap and loses to any net that actually separates the
    classes.  A gap can be nan when a (group, label) cell is empty in the
    split -- german's 37 non-foreign workers make this common -- so an
    undefined gap is treated as no penalty rather than poisoning the score."""
    gap = (metrics['equalized_odds_diff'] if args.method == 'eo'
           else metrics['demographic_parity_diff'])
    if not np.isfinite(gap):
        gap = 0.0
    util = metrics['balanced_accuracy']
    if not np.isfinite(util):
        util = stats['acc']
    return util - args.select_tradeoff * gap


def train_one(name, args):
    cfg = DATASETS[name]
    path = args.data or cfg['default_path']
    X, y, s, feature_names = cfg['loader'](path, drop_sensitive=args.drop_sensitive)

    print(f"\n{'='*80}")
    print(f"{name.upper()}  |  protected attribute: {cfg['attr']}  |  method: {args.method}")
    print(f"{'='*80}")
    print(f"  samples {X.shape[0]}, features {X.shape[1]}")
    print(f"  label balance      : {np.bincount(y).tolist()}")
    print(f"  group balance (s)  : {np.bincount(s).tolist()}  (index 1 = privileged)")

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)

    train_loader, val_loader, test_loader, scaler, splits = make_loaders(X, y, s, args)
    sens_idx = sensitive_index(feature_names, cfg['attr'])
    sens_enc = sensitive_encoding(X, scaler, sens_idx, cfg['attr'])

    layers = build_layers(args.arch_src, input_size=X.shape[1])
    model = ArchNet(layers, init=args.init).to(args.device)
    print("\nArchitecture:")
    print(model.model)

    out_dim = None
    for m in reversed(layers):
        if isinstance(m, nn.Linear):
            out_dim = m.out_features
            break
    if out_dim != 2:
        raise ValueError(f"architecture must end in a 2-way Linear output, got {out_dim}")

    adversary = adv_optimizer = None
    if args.method == 'adv':
        adversary = Adversary(2, args.adv_hidden).to(args.device)
        adv_optimizer = optim.Adam(adversary.parameters(), lr=args.adv_lr)

    if args.class_weight:
        counts = np.bincount(y, minlength=2).astype(np.float64)
        w = torch.tensor(counts.sum() / (2.0 * np.maximum(counts, 1)),
                         dtype=torch.float32, device=args.device)
        print(f"  class weights       : {w.tolist()}")
        criterion = nn.CrossEntropyLoss(weight=w)
    else:
        criterion = nn.CrossEntropyLoss()
    optimizer = (optim.Adam(model.parameters(), lr=args.lr)
                 if args.optimizer == 'adam'
                 else optim.SGD(model.parameters(), lr=args.lr, momentum=0.9))

    best_score, best_state, patience = -float('inf'), None, 0
    for epoch in range(1, args.epochs + 1):
        tr_stats, _ = run_epoch(model, adversary, train_loader, criterion,
                                optimizer, adv_optimizer, args, args.device, train=True)
        va_stats, (vy, vp, vs) = run_epoch(model, adversary, val_loader, criterion,
                                           optimizer, adv_optimizer, args, args.device,
                                           train=False)
        vm = fairness_metrics(vy, vp, vs)
        score = selection_score(va_stats, vm, args)

        if epoch % args.log_every == 0 or epoch == 1:
            print(f"  epoch {epoch:4d}  train loss {tr_stats['loss']:.4f} "
                  f"acc {tr_stats['acc']*100:5.2f}%  |  val acc {va_stats['acc']*100:5.2f}% "
                  f"bal {vm['balanced_accuracy']*100:5.2f}% "
                  f"dp {vm['demographic_parity_diff']:.4f} "
                  f"eo {vm['equalized_odds_diff']:.4f}  score {score:.4f}")

        if score > best_score + 1e-6:
            best_score, patience = score, 0
            best_state = {k: v.detach().clone() for k, v in model.state_dict().items()}
        else:
            patience += 1
            if patience >= args.patience:
                print(f"  early stop at epoch {epoch}")
                break

    if best_state is not None:
        model.load_state_dict(best_state)

    te_stats, (ty, tp, ts) = run_epoch(model, adversary, test_loader, criterion,
                                       optimizer, adv_optimizer, args, args.device,
                                       train=False)
    tm = fairness_metrics(ty, tp, ts)
    print_metrics(f"{name} test ({cfg['attr']})", te_stats['acc'], tm)
    if np.isfinite(tm['balanced_accuracy']) and tm['balanced_accuracy'] < 0.55:
        print("  WARNING: balanced accuracy is near 50% -- this net is close to a "
              "constant predictor, so its 0.0 fairness gap is trivial, not earned. "
              "Try --lambda-fair 0.3 --lr 0.01 --class-weight --epochs 400.")
    n_priv = int((ts == 1).sum())
    n_unpriv = int((ts == 0).sum())
    if min(n_priv, n_unpriv) < 30:
        print(f"  WARNING: the smaller protected group has only "
              f"{min(n_priv, n_unpriv)} rows in the test split; the fairness "
              f"gaps above carry very wide error bars.")
    if args.verbose:
        print(classification_report(ty, tp, zero_division=0))
        print(confusion_matrix(ty, tp))

    os.makedirs(args.outdir, exist_ok=True)
    tag = f"{name}_{args.method}"
    torch.save({'state_dict': model.state_dict(),
                'arch_src': args.arch_src,
                'input_size': X.shape[1],
                'feature_names': feature_names,
                'protected_attribute': cfg['attr'],
                'sensitive_index': sens_idx,
                'sensitive_encoding': sens_enc,
                'scaler_mean': scaler.mean_,
                'scaler_scale': scaler.scale_,
                'metrics': tm,
                'test_accuracy': te_stats['acc']},
               os.path.join(args.outdir, f"{tag}.pt"))
    print(f"\n  saved {os.path.join(args.outdir, tag + '.pt')}")

    if not args.no_onnx:
        os.makedirs(args.onnx_dir, exist_ok=True)
        onnx_path = os.path.join(args.onnx_dir, onnx_name(name, layers))
        model.eval()
        torch.onnx.export(model, torch.randn(1, X.shape[1], device=args.device),
                          onnx_path, export_params=True, opset_version=11,
                          do_constant_folding=False, input_names=["input"],
                          output_names=["output"], dynamic_axes=None)
        print(f"  saved {onnx_path}")

    if not args.no_dataset_txt:
        Xs, ys = splits[args.txt_split]
        txt_path = os.path.join(args.txt_dir, f"{name}_new.txt")
        save_dataset_txt(Xs, ys, txt_path)
        save_scaler_txt(scaler, feature_names,
                        os.path.join(args.txt_dir, f"{name}_scaler.txt"))
        # The dumped rows are already standardized, so feeding them straight
        # back through the network must reproduce the training-time numbers.
        if args.txt_split in ('all', 'test'):
            n_test = len(splits['test'][1])
            verify_txt_matches_model(txt_path, model, (n_test, te_stats['acc']),
                                     args.device)

    print_sensitive_encoding(sens_enc, X.shape[1])
    return te_stats['acc'], tm

# ==================== CLI ====================


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--dataset', default='all',
                   choices=list(DATASETS) + ['all'],
                   help="dataset to train on (default: all three)")
    p.add_argument('--data', default=None,
                   help="explicit CSV path (only meaningful with a single --dataset)")
    p.add_argument('--arch', default=None,
                   help="inline architecture, e.g. \"[nn.Linear(input_size,2), nn.ReLU(), "
                        "nn.Linear(2,4), nn.ReLU(), nn.Linear(4,2)]\"")
    p.add_argument('--arch-file', default=None,
                   help="python file binding `layers = [...]`")
    p.add_argument('--method', default='dp', choices=['none', 'dp', 'eo', 'adv'],
                   help="fairness mechanism (default: dp)")
    p.add_argument('--lambda-fair', type=float, default=1.0,
                   help="fairness weight; higher = fairer, usually less accurate")
    p.add_argument('--select-tradeoff', type=float, default=1.0,
                   help="weight on the validation fairness gap during model selection")
    p.add_argument('--drop-sensitive', action='store_true',
                   help="remove the protected attribute from the input features")
    p.add_argument('--epochs', type=int, default=200)
    p.add_argument('--batch-size', type=int, default=256)
    p.add_argument('--lr', type=float, default=1e-3)
    p.add_argument('--optimizer', default='adam', choices=['adam', 'sgd'])
    p.add_argument('--init', default='he', choices=['he', 'default'],
                   help="He init + small positive bias keeps narrow ReLU stacks alive")
    p.add_argument('--class-weight', action='store_true',
                   help="inverse-frequency class weights in the task loss")
    p.add_argument('--adv-lr', type=float, default=1e-3)
    p.add_argument('--adv-hidden', type=int, default=32)
    p.add_argument('--patience', type=int, default=25)
    p.add_argument('--test-size', type=float, default=0.2)
    p.add_argument('--seed', type=int, default=42)
    p.add_argument('--outdir', default=project_path('data/models'),
                   help="where the .pt checkpoint (weights + metrics) goes")
    p.add_argument('--onnx-dir', default=project_path('data/models'),
                   help="where <dataset>_<hidden widths>.onnx goes")
    p.add_argument('--no-onnx', action='store_true', help="skip the ONNX export")
    p.add_argument('--txt-dir', default=project_path('data/inputs'),
                   help="where <dataset>_new.txt goes")
    p.add_argument('--txt-split', default='all',
                   choices=['all', 'train', 'val', 'test'],
                   help="which rows land in <dataset>_new.txt (default: all)")
    p.add_argument('--no-dataset-txt', action='store_true',
                   help="skip the <dataset>_new.txt dump")
    p.add_argument('--log-every', type=int, default=10)
    p.add_argument('--verbose', action='store_true')
    p.add_argument('--device', default='cpu')
    args = p.parse_args(argv)

    if args.arch and args.arch_file:
        p.error("pass either --arch or --arch-file, not both")
    if args.arch_file:
        with open(args.arch_file) as f:
            args.arch_src = f.read()
    elif args.arch:
        args.arch_src = args.arch
    else:
        args.arch_src = DEFAULT_ARCH

    if args.data and args.dataset == 'all':
        p.error("--data only applies when a single --dataset is selected")
    return args


def main(argv=None):
    args = parse_args(argv)
    names = list(DATASETS) if args.dataset == 'all' else [args.dataset]

    results = {}
    for name in names:
        try:
            results[name] = train_one(name, args)
        except Exception as exc:
            print(f"\n[{name}] FAILED: {type(exc).__name__}: {exc}", file=sys.stderr)

    print(f"\n{'='*80}")
    print(f"SUMMARY  (method={args.method}, lambda={args.lambda_fair})")
    print(f"{'='*80}")
    print(f"{'dataset':<10}{'attribute':<18}{'acc':>8}{'DP gap':>10}{'EOdds gap':>12}{'DI ratio':>10}")
    for name, (acc, m) in results.items():
        print(f"{name:<10}{DATASETS[name]['attr']:<18}{acc*100:7.2f}%"
              f"{m['demographic_parity_diff']:10.4f}{m['equalized_odds_diff']:12.4f}"
              f"{m['disparate_impact']:10.4f}")


if __name__ == '__main__':
    main()
