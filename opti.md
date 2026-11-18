# Constrained Nonlinear Programming (NLP): Master Algorithm & Question Types

Consider the general constrained nonlinear programming problem:

$$\text{Minimize } f(\mathbf{x}) \quad \text{subject to } g_i(\mathbf{x}) \le 0 \; (i=1,\dots,m), \quad h_j(\mathbf{x}) = 0 \; (j=1,\dots,l)$$

---

## Part 1: Step-by-Step Decision Tree & Master Algorithm

```
                  [ Given Candidate Point x* ]
                               │
                   1. Check Primal Feasibility
                               │
           ┌───────────────────┴───────────────────┐
      [Infeasible]                            [Feasible]
           │                                       │
  (Stop: x* cannot be                     2. Identify Binding 
   a local minimum)                     Constraints I & Active Set
                                                   │
                                     3. Check Constraint Qualification 
                                                  (LICQ)
                                                   │
                         ┌─────────────────────────┴─────────────────────────┐
                     [LICQ Holds]                                      [LICQ Fails]
                         │                                                   │
              4A. Set up KKT Equations                            4B. Set up FJ Equations
         ∇f + Σ uᵢ ∇gᵢ + Σ vⱼ ∇hⱼ = 0                        u₀ ∇f + Σ uᵢ ∇gᵢ + Σ vⱼ ∇hⱼ = 0
            with u₀ = 1 strictly                                allowing u₀ = 0 or u₀ > 0
                         │                                                   │
            ┌────────────┴────────────┐                        ┌─────────────┴─────────────┐
        [KKT Holds]              [KKT Fails]               [FJ Holds]                 [FJ Fails]
            │                         │                        │                          │
   5. Check Convexity         (x* is NOT a             x* is an FJ point         (Stop: x* is NOT a
     of f, gᵢ, hⱼ              local min)             (Check if u₀>0 to           local minimum)
            │                                          see if KKT holds)
    ┌───────┴───────┐
 [Convex]       [Non-Convex]
    │               │
(GLOBAL          6. Check SONC
MINIMUM)       (Reduced Hessian)
```

---

### Step 1: Check Primal Feasibility
* **Action:** Evaluate $g_i(\mathbf{x}^*)$ and $h_j(\mathbf{x}^*)$.
* **Outcome:**
  * If $g_i(\mathbf{x}^*) > 0$ for any $i$ OR $h_j(\mathbf{x}^*) \neq 0$ for any $j$, **Stop.** The point is infeasible and cannot be a local minimum.
  * If $g_i(\mathbf{x}^*) \le 0$ for all $i$ AND $h_j(\mathbf{x}^*) = 0$ for all $j$, proceed to Step 2.

---

### Step 2: Identify Binding (Active) Constraints
* **Action:** Partition inequality constraints into two sets:
  * **Binding/Active ($I$):** $g_i(\mathbf{x}^*) = 0$
  * **Non-binding/Inactive ($I^*$):** $g_i(\mathbf{x}^*) < 0$ (set $u_i = 0$ immediately by Complementary Slackness)

---

### Step 3: Check Constraint Qualifications (LICQ)
* **Action:** Collect the gradient vectors of all active constraints:
  $$\{\nabla g_i(\mathbf{x}^*)\}_{i \in I} \quad \cup \quad \{\nabla h_j(\mathbf{x}^*)\}_{j=1}^l$$
  Form a matrix with these vectors as rows and compute its rank.
* **Outcome:**
  * **LICQ Holds (Vectors are Linearly Independent):** Proceed to **Step 4A (KKT Analysis)**. You are guaranteed that $u_0 \neq 0$ in the Fritz John equations.
  * **LICQ Fails (Vectors are Linearly Dependent):** Proceed to **Step 4B (Fritz John Analysis)**. KKT might fail even if $\mathbf{x}^*$ is a true local minimum.

---

### Step 4A: Check KKT Necessary Conditions (when LICQ holds)
* **Action:** Solve the linear system for multipliers $u_i$ ($i \in I$) and $v_j$:
  $$\nabla f(\mathbf{x}^*) + \sum_{i \in I} u_i \nabla g_i(\mathbf{x}^*) + \sum_{j=1}^l v_j \nabla h_j(\mathbf{x}^*) = \mathbf{0}$$
* **Outcome:**
  * **If $u_i \ge 0$ for all active $i \in I$:** $\mathbf{x}^*$ is a **KKT Point**. Move to Step 5.
  * **If any $u_i < 0$:** **Stop.** $\mathbf{x}^*$ violates dual feasibility and is **NOT a local minimum**.

---

### Step 4B: Check Fritz John (FJ) Conditions (when LICQ fails)
* **Action:** Solve for $u_0 \ge 0, u_i \ge 0 \; (i \in I)$, and $v_j$ (unrestricted in sign) not all zero:
  $$u_0 \nabla f(\mathbf{x}^*) + \sum_{i \in I} u_i \nabla g_i(\mathbf{x}^*) + \sum_{j=1}^l v_j \nabla h_j(\mathbf{x}^*) = \mathbf{0}$$
* **Outcome:**
  * **If solved with $u_0 > 0$:** Divide throughout by $u_0$ to normalize $u_0 = 1$. $\mathbf{x}^*$ is also a **KKT Point**. Move to Step 5.
  * **If solved ONLY with $u_0 = 0$:** $\mathbf{x}^*$ is an **FJ point, but NOT a KKT point**. KKT conditions cannot be used here to judge optimality.
  * **If no non-trivial solution exists:** **Stop.** $\mathbf{x}^*$ is **NOT a local minimum**.

---

### Step 5: Check Convexity (Global Optimality Test)
* **Action:** Compute the Hessian matrices $\nabla^2 f(\mathbf{x})$ and $\nabla^2 g_i(\mathbf{x})$ for all $i$. Check if they are Positive Semidefinite (PSD).
* **Outcome:**
  * **If $f(\mathbf{x})$ is Convex AND all $g_i(\mathbf{x})$ are Convex (and $h_j$ are affine):**
    By KKT Sufficiency (Theorem 9/10), any KKT point $\mathbf{x}^*$ is guaranteed to be a **GLOBAL MINIMUM**. (No further checks needed).
  * **If Non-Convex:** KKT is only a necessary condition. Move to Step 6 for second-order verification.

---

### Step 6: Second-Order Conditions for Non-Convex Problems
* **Action:** Construct the Tangent Space $T$ of the active constraints:
  $$T = \{\mathbf{d} \in \mathbb{R}^n : \nabla g_i(\mathbf{x}^*) \mathbf{d} = 0 \; (i \in I), \; \nabla h_j(\mathbf{x}^*) \mathbf{d} = 0 \; (j=1,\dots,l)\}$$
  Form the Lagrangian Hessian: 
  $$\nabla^2_{xx} L(\mathbf{x}^*, \mathbf{u}, \mathbf{v}) = \nabla^2 f(\mathbf{x}^*) + \sum_{i \in I} u_i \nabla^2 g_i(\mathbf{x}^*) + \sum_{j=1}^l v_j \nabla^2 h_j(\mathbf{x}^*)$$
* **Outcome:**
  * **SONC:** If $\mathbf{d}^T \nabla^2_{xx} L \mathbf{d} \ge 0$ for all $\mathbf{d} \in T$, Second-Order Necessary Conditions hold.
  * **SOSC:** If $\mathbf{d}^T \nabla^2_{xx} L \mathbf{d} > 0$ for all non-zero $\mathbf{d} \in T$, $\mathbf{x}^*$ is a **STRICT LOCAL MINIMUM**.

---

## Part 2: All Possible Question Types & Solution Strategies

Beyond testing specific candidate points, exam and assignment questions in constrained optimization generally fall into 6 distinct categories:

### Type 1: "Find all FJ and KKT points of a given NLP"
* **Strategy:**
  1. Set up the general system: 
     $$u_0 \nabla f(\mathbf{x}) + \sum_{i=1}^m u_i \nabla g_i(\mathbf{x}) = \mathbf{0} \quad \text{with } u_i g_i(\mathbf{x}) = 0$$
  2. Perform a case-by-case analysis based on active constraint combinations ($2^m$ combinations of $g_i(\mathbf{x}) = 0$ vs. $g_i(\mathbf{x}) < 0$).
  3. For each case, solve the algebraic system. If $u_0$ can be set to $1$, the solution is a **KKT point**. If $u_0$ is forced to be $0$, it is an **FJ-only point**.

---

### Type 2: "Is KKT a Necessary Condition for this problem?"
* **Strategy:**
  1. Find the local minimum $\mathbf{x}^*$ (geometrically or analytically).
  2. Check if LICQ or Mangasarian-Fromovitz ($G_{0, \mathbf{x}^*} \neq \emptyset$) holds at $\mathbf{x}^*$.
  3. **Conclusion:** If LICQ holds, KKT is necessary. If LICQ fails and no $u_0 > 0$ solution exists, KKT is **NOT** a necessary condition for that specific problem (e.g., $\min -x_1$ s.t. $x_2 - (1-x_1)^3 \le 0, -x_2 \le 0$ at $[1,0]^T$).

---

### Type 3: "Is KKT a Sufficient Condition for this problem?"
* **Strategy:**
  1. Check the convexity of $f(\mathbf{x})$ and $g_i(\mathbf{x})$ across the domain (via Hessians).
  2. **Conclusion:** If $f$ and all $g_i$ are convex, KKT is sufficient (every KKT point is a global minimum). If any function is non-convex, construct a counterexample (a stationary/KKT point that is a saddle point or local maximum) to show KKT is **NOT** sufficient.

---

### Type 4: "Formulate KKT Conditions for Linear Programming (LPP)"
* **Problem:** $\text{Min } \mathbf{c}^T \mathbf{x} \text{ s.t. } A\mathbf{x} \le \mathbf{b}, \; \mathbf{x} \ge \mathbf{0}$.
* **Strategy:**
  1. Rewrite constraints in standard $\le 0$ form: $g_1(\mathbf{x}) = A\mathbf{x} - \mathbf{b} \le \mathbf{0}$ and $g_2(\mathbf{x}) = -\mathbf{x} \le \mathbf{0}$.
  2. KKT Stationarity: $\mathbf{c} + A^T \mathbf{y} - \mathbf{v} = \mathbf{0} \implies A^T \mathbf{y} \le \mathbf{c}$ (where $\mathbf{y} \ge \mathbf{0}, \mathbf{v} \ge \mathbf{0}$).
  3. Complementary Slackness: $y_i (A\mathbf{x} - \mathbf{b})_i = 0$ and $v_j x_j = 0$.
* **Key Result:** In LPP, the KKT conditions are **identical to Dual Feasibility and Complementary Slackness**, proving that $\mathbf{x}^*$ is optimal $\iff$ it is a KKT point.

---

### Type 5: "Check if the Linearized Cone $G_{0, \mathbf{x}^*}$ Equals the Feasible Cone $D_{\mathbf{x}^*}$"
* **Strategy:**
  1. Compute $D_{\mathbf{x}^*}$ using exact directions where $g_i(\mathbf{x}^* + t\mathbf{d}) \le 0$ for small $t > 0$.
  2. Compute $G_{0, \mathbf{x}^*}$ using strict inner product inequalities $\nabla g_i(\mathbf{x}^*) \mathbf{d} < 0$ for $i \in I$.
  3. **Note:** $G_{0, \mathbf{x}^*}$ is an open cone, whereas $D_{\mathbf{x}^*}$ often includes boundary directions where $\nabla g_i(\mathbf{x}^*) \mathbf{d} = 0$. Thus, $G_{0, \mathbf{x}^*} \subseteq D_{\mathbf{x}^*}$ always holds (under differentiability), but equality $G_{0, \mathbf{x}^*} = D_{\mathbf{x}^*}$ frequently fails at boundaries or cusp points.

---

### Type 6: "Equality Constraints via Lagrange Multipliers"
* **Problem:** $\text{Min } f(\mathbf{x}) \text{ s.t. } h_j(\mathbf{x}) = 0 \; (j=1,\dots,l)$.
* **Strategy:**
  1. Write the Lagrangian: $L(\mathbf{x}, \mathbf{v}) = f(\mathbf{x}) + \sum_{j=1}^l v_j h_j(\mathbf{x})$.
  2. Set the gradient to zero: $\nabla_{\mathbf{x}} L = \mathbf{0} \implies \nabla f(\mathbf{x}^*) + \sum_{j=1}^l v_j \nabla h_j(\mathbf{x}^*) = \mathbf{0}$.
  3. Note that multipliers $v_j$ associated with equality constraints are **unrestricted in sign** (they can be positive, negative, or zero).
