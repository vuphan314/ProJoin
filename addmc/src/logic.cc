#include "logic.hh"

/* global vars ============================================================== */

TimePoint toolStartPoint;

bool weightedCounting;
bool projectedCounting;
bool maxsatSolving;
bool minMaxsatSolving;
Int randomSeed;
Int maxsatBound;
bool multiplePrecision;
bool logCounting;
Int verboseCnf;
Int verboseSolving;

/* namespaces =============================================================== */

/* namespace util =========================================================== */

string util::useDdPackage(string ddPackageArg) {
  assert(DD_PACKAGES.contains(ddPackageArg));
  return " [with " + DD_PACKAGE_OPTION + "_arg = " + ddPackageArg + "]";
}

map<Int, string> util::getVarOrderHeuristics() {
  map<Int, string> m = CNF_VAR_ORDER_HEURISTICS;
  m.insert(JOIN_TREE_VAR_ORDER_HEURISTICS.begin(), JOIN_TREE_VAR_ORDER_HEURISTICS.end());
  return m;
}

string util::helpVarOrderHeuristic(string prefix) {
  map<Int, string> heuristics = CNF_VAR_ORDER_HEURISTICS;
  string s = prefix + " var order";

  if (prefix == "slice") {
    s += useDdPackage(CUDD);
    heuristics = getVarOrderHeuristics();
  }
  else {
    assert(prefix == "diagram" || prefix == "cluster");
  }

  s += ": ";
  for (auto it = heuristics.begin(); it != heuristics.end(); it++) {
    s += to_string(it->first) + "/" + it->second;
    if (next(it) != heuristics.end()) {
      s += ", ";
    }
  }
  return s + " (negative for inverse order); int";
}

string util::helpVerboseSolving() {
  return "verbose solving: 0, 1, 2; int";
}

TimePoint util::getTimePoint() {
  return std::chrono::steady_clock::now();
}

Float util::getDuration(TimePoint start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(getTimePoint() - start).count() / 1e3;
}

vector<string> util::splitInputLine(string line) {
  std::istringstream inStringStream(line);
  vector<string> words;
  copy(istream_iterator<string>(inStringStream), istream_iterator<string>(), back_inserter(words));
  return words;
}

void util::printInputLine(string line, Int lineIndex) {
  cout << "c line " << right << setw(5) << lineIndex << ":" << (line.empty() ? "" : " " + line) << "\n";
}

void util::printRowKey(string key, size_t keyWidth) {
  if (key != "s") {
    key = "c " + key;
  }

  keyWidth = max(keyWidth, key.size() + 1);

  cout << left << setw(keyWidth) << key;
}

void util::printPreciseFloat(Float f) {
  Int p = cout.precision();
  // cout.precision(std::numeric_limits<Float>::max_digits10);
  cout << f;
  cout.precision(p);
}

void util::printPreciseFloatRow(string key, Float f, size_t keyWidth) {
  printRowKey(key, keyWidth);
  printPreciseFloat(f);
  cout << "\n";
}

/* classes for exceptions =================================================== */

/* class EmptyClauseException =============================================== */

EmptyClauseException::EmptyClauseException(Int lineIndex, string line) {
  cout << WARNING << "empty clause | line " << lineIndex << ": " << line << "\n";
}

/* classes for cnf formulas ================================================= */

/* class Number ============================================================= */

Number::Number(const mpq_class& q) {
  assert(multiplePrecision);
  quotient = q;
}

Number::Number(Float f) {
  assert(!multiplePrecision);
  fraction = f;
}

Number::Number(const Number& n) {
  if (multiplePrecision) {
    quotient = n.quotient;
  }
  else {
    fraction = n.fraction;
  }
}

Number::Number(string s) {
  Int divPos = s.find('/');
  if (multiplePrecision) {
    if (divPos != string::npos) { // `s` is "{int1}/{int2}"
      *this = Number(mpq_class(s));
    }
    else { // `s` is "{float1}"
      *this = Number(mpq_class(mpf_class(s)));
    }
  }
  else {
    if (divPos != string::npos) {
      Float numerator = stold(s.substr(0, divPos));
      Float denominator = stold(s.substr(divPos + 1));
      *this = Number(numerator / denominator);
    }
    else {
      *this = Number(stold(s));
    }
  }
}

Float Number::getLogSumExp(const Number& n) const {
  assert(logCounting);
  if (fraction == -INF) {
    return n.fraction;
  }
  else if (n.fraction == -INF) {
    return fraction;
  }
  Float m = max(fraction, n.fraction);
  return log10l(exp10l(fraction - m) + exp10l(n.fraction - m)) + m; // base-10 Cudd_addLogSumExp
}

Float Number::getLog10() const {
  if (multiplePrecision) {
    mpf_t f; // C interface
    mpf_init(f);
    mpf_set_q(f, quotient.get_mpq_t());
    long int exponent;
    Float d = mpf_get_d_2exp(&exponent, f); // f == d * 2^exponent
    Float lgF = log10l(d) + exponent * log10l(2);
    mpf_clear(f);
    return lgF;
  }
  return log10l(fraction);
}

bool Number::operator==(const Number& n) const {
  if (multiplePrecision) {
    return quotient == n.quotient;
  }
  return fraction == n.fraction;
}

bool Number::operator!=(const Number& n) const {
  return !(*this == n);
}

bool Number::operator<(const Number& n) const {
  if (multiplePrecision) {
    return quotient < n.quotient;
  }
  return fraction < n.fraction;
}

bool Number::operator<=(const Number& n) const {
  return *this < n || *this == n;
}

bool Number::operator>=(const Number& n) const {
  if (multiplePrecision) {
    return quotient >= n.quotient;
  }
  return fraction >= n.fraction;
}

Number Number::operator*(const Number& n) const {
  if (multiplePrecision) {
    return Number(quotient * n.quotient);
  }
  return Number(fraction * n.fraction);
}

Number& Number::operator*=(const Number& n) {
  *this = *this * n;
  return *this;
}

Number Number::operator+(const Number& n) const {
  if (multiplePrecision) {
    return Number(quotient + n.quotient);
  }
  return Number(fraction + n.fraction);
}

Number& Number::operator+=(const Number& n) {
  *this = *this + n;
  return *this;
}

Number Number::operator-(const Number& n) const {
  if (multiplePrecision) {
    return Number(quotient - n.quotient);
  }
  return Number(fraction - n.fraction);
}

/* class Graph ============================================================== */

Graph::Graph(const Set<Int>& vs) {
  for (Int v : vs) {
    vertices.insert(v);
    adjacencyMap[v] = Set<Int>();
  }
}

void Graph::addEdge(Int v1, Int v2) {
  adjacencyMap.at(v1).insert(v2);
  adjacencyMap.at(v2).insert(v1);
}

bool Graph::isNeighbor(Int v1, Int v2) const {
  return adjacencyMap.at(v1).contains(v2);
}

bool Graph::hasPath(Int from, Int to, Set<Int>& visitedVertices) const {
  if (from == to) {
    return true;
  }

  visitedVertices.insert(from);

  Set<Int> unvisitedNeighbors = util::getDiff(adjacencyMap.at(from), visitedVertices);

  for (Int v : unvisitedNeighbors) {
    if (hasPath(v, to, visitedVertices)) {
      return true;
    }
  }
  return false;
}

bool Graph::hasPath(Int from, Int to) const {
  Set<Int> visitedVertices;
  return hasPath(from, to, visitedVertices);
}

void Graph::removeVertex(Int v) {
  vertices.erase(v);

  adjacencyMap.erase(v); // edges from v

  for (pair<const Int, Set<Int>>& vertexAndNeighbors : adjacencyMap) {
    vertexAndNeighbors.second.erase(v); // edge to v
  }
}

void Graph::fillInEdges(Int v) {
  const Set<Int>& neighbors = adjacencyMap.at(v);
  for (auto neighbor1 = neighbors.begin(); neighbor1 != neighbors.end(); neighbor1++) {
    for (auto neighbor2 = next(neighbor1); neighbor2 != neighbors.end(); neighbor2++) {
      addEdge(*neighbor1, *neighbor2);
    }
  }
}

Int Graph::countFillInEdges(Int v) const {
  Int count = 0;
  const Set<Int>& neighbors = adjacencyMap.at(v);
  for (auto neighbor1 = neighbors.begin(); neighbor1 != neighbors.end(); neighbor1++) {
    for (auto neighbor2 = next(neighbor1); neighbor2 != neighbors.end(); neighbor2++) {
      if (!isNeighbor(*neighbor1, *neighbor2)) {
        count++;
      }
    }
  }
  return count;
}

Int Graph::getMinfillVertex() const {
  Int vertex = MIN_INT;
  Int fillInEdgeCount = MAX_INT;

  for (Int v : vertices) {
    Int count = countFillInEdges(v);
    if (count < fillInEdgeCount) {
      fillInEdgeCount = count;
      vertex = v;
    }
  }

  if (vertex == MIN_INT) {
    throw MyError("graph has no vertex");
  }

  return vertex;
}

/* class Label ============================================================== */

void Label::addNumber(Int i) {
  push_back(i);
  sort(begin(), end(), greater<Int>());
}

bool Label::hasSmallerLabel(const pair<Int, Label>& a, const pair <Int, Label>& b) {
  return a.second < b.second;
}

/* class Clause ============================================================= */

void Clause::printClause() const {
  for (auto it = begin(); it != end(); it++) {
    cout << " " << right << setw(5) << *it;
  }
  cout << "\n";
}

Set<Int> Clause::getClauseVars() const {
  Set<Int> vars;
  for (Int literal : *this) {
    vars.insert(abs(literal));
  }
  return vars;
}

/* class Cnf ================================================================ */

void Cnf::printClauses() const {
  cout << "c cnf formula:\n";
  for (Int i = 0; i < clauses.size(); i++) {
    cout << "c  clause " << right << setw(5) << i + 1 << ":";
    clauses.at(i).printClause();
  }
}

void Cnf::printLiteralWeights() const {
  cout << "c literal weights:\n";
  for (Int var = 1; var <= declaredVarCount; var++) {
    cout << "c  weight " << right << setw(5) << var << ": " << literalWeights.at(var) << "\n";
    cout << "c  weight " << right << setw(5) << -var << ": " << literalWeights.at(-var) << "\n";
  }
}

Set<Int> Cnf::getDisjunctiveVars() const {
  Set<Int> disjunctiveVars;
  for (Int var = 1; var <= declaredVarCount; var++) {
    if (!additiveVars.contains(var)) {
      disjunctiveVars.insert(var);
    }
  }
  return disjunctiveVars;
}



void Cnf::addClause(const Clause& clause, char type, double weight, int comparator, Map<Int, Int> coefs, int k ){
  Int clauseIndex = clauses.size();
  clauses.push_back(clause);
  types.push_back(type);
  weights.push_back(weight);
  coefLists.push_back(coefs);
  comparators.push_back(comparator);
  klist.push_back(k);
  for (Int literal : clause) {
    Int var = abs(literal);
    auto it = varToClauses.find(var);
    if (it != varToClauses.end()) {
      it->second.insert(clauseIndex);
    }
    else {
      varToClauses[var] = Set<Int>{clauseIndex};
    }
  }
}

void Cnf::setApparentVars() {
  for (const pair<Int, Set<Int>>& kv : varToClauses) {
    apparentVars.insert(kv.first);
  }
}

Graph Cnf::getPrimalGraph() const {
  Graph graph(apparentVars);
  for (const Clause& clause : clauses) {
    for (auto literal1 = clause.begin(); literal1 != clause.end(); literal1++) {
      for (auto literal2 = next(literal1); literal2 != clause.end(); literal2++) {
        Int var1 = abs(*literal1);
        Int var2 = abs(*literal2);
        graph.addEdge(var1, var2);
      }
    }
  }
  return graph;
}

vector<Int> Cnf::getRandomVarOrder() const {
  vector<Int> varOrder(apparentVars.begin(), apparentVars.end());
  std::mt19937 generator;
  generator.seed(randomSeed);
  shuffle(varOrder.begin(), varOrder.end(), generator);
  return varOrder;
}

vector<Int> Cnf::getDeclaredVarOrder() const {
  vector<Int> varOrder;
  for (Int var = 1; var <= declaredVarCount; var++) {
    if (apparentVars.contains(var)) {
      varOrder.push_back(var);
    }
  }
  return varOrder;
}

vector<Int> Cnf::getMostClausesVarOrder() const {
  multimap<Int, Int, greater<Int>> m; // clause count |-> var
  for (const pair<Int, Set<Int>>& varAndSet : varToClauses) {
    m.insert({varAndSet.second.size(), varAndSet.first});
  }

  vector<Int> varOrder;
  for (pair<Int, Int> sizeAndVar : m) {
    varOrder.push_back(sizeAndVar.second);
  }
  return varOrder;
}

vector<Int> Cnf::getMinfillVarOrder() const {
  vector<Int> varOrder;

  Graph graph = getPrimalGraph();
  while (!graph.vertices.empty()) {
    Int vertex = graph.getMinfillVertex();
    graph.fillInEdges(vertex);
    graph.removeVertex(vertex);
    varOrder.push_back(vertex);
  }

  return varOrder;
}

vector<Int> Cnf::getMcsVarOrder() const {
  Graph graph = getPrimalGraph();

  auto startVertex = graph.vertices.begin();
  if (startVertex == graph.vertices.end()) {
    return vector<Int>();
  }

  Map<Int, Int> rankedNeighborCounts; // unranked vertex |-> number of ranked neighbors
  for (auto it = next(startVertex); it != graph.vertices.end(); it++) {
    rankedNeighborCounts[*it] = 0;
  }

  Int bestVertex = *startVertex;
  Int bestRankedNeighborCount = MIN_INT;

  vector<Int> varOrder;
  do {
    varOrder.push_back(bestVertex);

    rankedNeighborCounts.erase(bestVertex);

    for (Int n : graph.adjacencyMap.at(bestVertex)) {
      auto entry = rankedNeighborCounts.find(n);
      if (entry != rankedNeighborCounts.end()) {
        entry->second++;
      }
    }

    bestRankedNeighborCount = MIN_INT;
    for (pair<Int, Int> entry : rankedNeighborCounts) {
      if (entry.second > bestRankedNeighborCount) {
        bestRankedNeighborCount = entry.second;
        bestVertex = entry.first;
      }
    }
  }
  while (bestRankedNeighborCount != MIN_INT);

  return varOrder;
}

vector<Int> Cnf::getLexpVarOrder() const {
  Map<Int, Label> unnumberedVertices;
  for (Int vertex : apparentVars) {
    unnumberedVertices[vertex] = Label();
  }
  vector<Int> numberedVertices; // whose alpha numbers are decreasing
  Graph graph = getPrimalGraph();
  for (Int number = apparentVars.size(); number > 0; number--) {
    Int vertex = max_element(unnumberedVertices.begin(), unnumberedVertices.end(), Label::hasSmallerLabel)->first; // ignores label
    numberedVertices.push_back(vertex);
    unnumberedVertices.erase(vertex);
    for (Int neighbor : graph.adjacencyMap.at(vertex)) {
      auto unnumberedNeighborIt = unnumberedVertices.find(neighbor);
      if (unnumberedNeighborIt != unnumberedVertices.end()) {
        Int unnumberedNeighbor = unnumberedNeighborIt->first;
        unnumberedVertices.at(unnumberedNeighbor).addNumber(number);
      }
    }
  }
  return numberedVertices;
}

vector<Int> Cnf::getLexmVarOrder() const {
  Map<Int, Label> unnumberedVertices;
  for (Int vertex : apparentVars) {
    unnumberedVertices[vertex] = Label();
  }
  vector<Int> numberedVertices; // whose alpha numbers are decreasing
  Graph graph = getPrimalGraph();
  for (Int i = apparentVars.size(); i > 0; i--) {
    Int v = max_element(unnumberedVertices.begin(), unnumberedVertices.end(), Label::hasSmallerLabel)->first; // ignores label
    numberedVertices.push_back(v);
    unnumberedVertices.erase(v);

    /* updates numberedVertices: */
    Graph subgraph = getPrimalGraph(); // will only contain v, w, and unnumbered vertices whose labels are less than w's label
    for (pair<const Int, Label>& wAndLabel : unnumberedVertices) {
      Int w = wAndLabel.first;
      Label& wLabel = wAndLabel.second;

      /* removes numbered vertices except v: */
      for (Int numberedVertex : numberedVertices) {
        if (numberedVertex != v) {
          subgraph.removeVertex(numberedVertex);
        }
      }

      /* removes each non-w unnumbered vertex whose label is not less than w's labels */
      for (const pair<Int, Label>& kv : unnumberedVertices) {
        Int unnumberedVertex = kv.first;
        const Label& label = kv.second;
        if (unnumberedVertex != w && label >= wLabel) {
          subgraph.removeVertex(unnumberedVertex);
        }
      }

      if (subgraph.hasPath(v, w)) {
        wLabel.addNumber(i);
      }
    }
  }
  return numberedVertices;
}

vector<Int> Cnf::getCnfVarOrder(Int cnfVarOrderHeuristic) const {
  vector<Int> varOrder;
  switch (abs(cnfVarOrderHeuristic)) {
    case RANDOM:
      varOrder = getRandomVarOrder();
      break;
    case DECLARED:
      varOrder = getDeclaredVarOrder();
      break;
    case MOST_CLAUSES:
      varOrder = getMostClausesVarOrder();
      break;
    case MINFILL:
      varOrder = getMinfillVarOrder();
      break;
    case MCS:
      varOrder = getMcsVarOrder();
      break;
    case LEXP:
      varOrder = getLexpVarOrder();
      break;
    default:
      assert(abs(cnfVarOrderHeuristic) == LEXM);
      varOrder = getLexmVarOrder();
  }

  if (cnfVarOrderHeuristic < 0) {
    reverse(varOrder.begin(), varOrder.end());
  }

  return varOrder;
}

static void PB_canonicalize(Set<Int>& clause, Map<Int,Int>& coefs, int *k, int *comparator){ // comparator: >= (1), =(2), <= (3)
  if (*comparator == 3){
    *comparator = 1;
    *k = -(*k);
    for (auto itr = clause.begin(); itr != clause.end(); ++itr) {
      int var = *itr;
      coefs[var] *= -1;
    }
  }
  vector<Int> tempVar;
  for (auto itr = clause.begin(); itr != clause.end(); ++itr) {
    int var = *itr;
    if (coefs[var] < 0){
      coefs.insert(pair<int,int> (-var, -coefs[var]));
      tempVar.push_back(-var);
      (*k) -= coefs[var];
      coefs.erase(var);
    }
  }
  for (auto var : tempVar){
    clause.insert(var);
    clause.erase(-var);
  }
}

Cnf::Cnf() {}

Cnf::Cnf(string filePath) {
  cout << "c processing cnf formula...\n";

  std::ifstream inputFileStream(filePath);
  if (!inputFileStream.is_open()) {
    throw MyError("unable to open file '", filePath, "'");
  }

  Int declaredClauseCount = MIN_INT;
  Int processedClauseCount = 0;

  Int lineIndex = 0;
  Int problemLineIndex = MIN_INT;

  string line;
  int wcnfFlag = 0; // flag indicates whether the instance is in WCNF (weighted MaxSAT)
  int hwcnfFlag = 0; // flag indicates whether the instance is in hybrid WCNF (hybrid weighted MaxSAT)
  while (getline(inputFileStream, line)) {
    lineIndex++;
    std::istringstream inStringStream(line);

    if (verboseCnf >= RAW_INPUT) {
      util::printInputLine(line, lineIndex);
    }
    vector<string> words = util::splitInputLine(line);
    if (words.empty()) {
      continue;
    }
    else if (words.front() == "p") { // problem line
      if (problemLineIndex != MIN_INT) {
        throw MyError("multiple problem lines: ", problemLineIndex, " and ", lineIndex);
      }
      problemLineIndex = lineIndex;

      if (words.size() < 4) {
        throw MyError("problem line ", lineIndex, " has ", words.size(), " words (should be at least 4)");
      }

      declaredVarCount = stoll(words.at(2));
      declaredClauseCount = stoll(words.at(3));
      wcnfFlag = (words.at(1) == "wcnf") ? 1 : 0;
      hwcnfFlag = (words.at(1) == "hwcnf") ? 1 : 0;
      if (hwcnfFlag == 1) wcnfFlag = 1;
      if (wcnfFlag == 1){
        std::cout<<"c Solving an weighted MaxSAT instance\n";
        if (words.size() == 5){
          trivialBoundPartialMaxSAT = stoll(words.at(4)); // the trivial bound given by a partial MaxSAT problem for ADD pruning
          std::cout<<"c trivial bound: "<<trivialBoundPartialMaxSAT<<std::endl;
        }
      }
    }
    else if ( (words.front() == "*") && (words.at(1) == "#variable=") ) { // problem line of a WBO/PBO file
      declaredVarCount = std::stoll(words.at(2));
      declaredClauseCount = stoll(words.at(4));
      trivialBoundPartialMaxSAT = stoll(words.at(12)); // the trivial bound given by a WBO problem for ADD pruning
      std::cout<<"c trivial bound: "<<trivialBoundPartialMaxSAT<<std::endl;
      problemLineIndex = lineIndex;
    }
    else if (Set<string>{"w", "vp", "c", "vm"}.contains(words.front())) { // possibly weight line or show line
      if (weightedCounting && (words.front() == "w" || (words.size() > 4 && words.at(1) == "p" && words.at(2) == "weight"))) { // weight line optionally ends with "0"
        if (problemLineIndex == MIN_INT) {
          throw MyError("no problem line before weighted literal | line ", lineIndex, ": ", line);
        }

        Int literal = stoll(words.at(words.front() == "w" ? 1 : 3));

        if (abs(literal) > declaredVarCount) {
          throw MyError("literal '", literal, "' inconsistent with declared var count '", declaredVarCount, "' | line ", lineIndex);
        }

        Number weight(words.at(words.front() == "w" ? 2: 4));
        if (weight < Number()) {
          throw MyError("weight must be non-negative | line ", lineIndex);
        }
        literalWeights[literal] = weight;
      }
      // support for MinMaxSAT is added by reading a "vm" line for min variables
      else if ( (projectedCounting || maxsatSolving)  && (words.front() == "vp" || words.front() == "vm" || (words.size() > 3 && words.at(1) == "p" && words.at(2) == "show"))) { // show line optionally ends with "0"
        if (problemLineIndex == MIN_INT) {
          throw MyError("no problem line before projected var | line ", lineIndex, ": ", line);
        }

        for (Int i = ( (words.front() == "vp" || words.front() == "vm") ? 1 : 3); i < words.size(); i++) {
          minMaxsatSolving = maxsatSolving; // set minMaxsatSolving flag to true
          Int num = stoll(words.at(i));
          if (num == 0) {
            if (i != words.size() - 1) {
              throw MyError("additive vars terminated prematurely by '0' | line ", lineIndex);
            }
          }
          else if (num < 0 || num > declaredVarCount) {
            throw MyError("var '", num, "' inconsistent with declared var count '", declaredVarCount, "' | line ", lineIndex);
          }
          else {
            additiveVars.insert(num);
          }
        }
      }
    }
    else if (Set<string>{"s", "INDETERMINATE"}.contains(words.front())) { // preprocessor pmc
      throw MyError("unexpected output from preprocessor pmc | line ", lineIndex, ": ", line);
    }
    else if ( (!words.front().starts_with("c")) && (!words.front().starts_with("*")) && (!words.front().starts_with("soft")) ) { // clause line
      if (problemLineIndex == MIN_INT) {
        throw MyError("no problem line before clause | line ", lineIndex);
      }
      double weight = 1;  // default constraint weight is 1
      char type = 'c'; // constraints are CNF clauses by default
      // std::cout<<"front "<<words.front()<<std::endl;
      if (hwcnfFlag == 1){
        Clause clause;
        string firstWord = words.at(0);
        weight = stod( firstWord.substr(1,firstWord.length() -2 ));
        words.erase(words.begin());
        if  ( words.at(1).starts_with("x")){  // WBO/PBO constraint
          type = 'p';
          Map<Int, Int> coefs;
          for( int i = 0; i < (words.size() - 3) / 2; i++){
            std::string str = words.at(i*2+1);
            Int var = std::stoll(str.substr(1,str.length()));
            Int coef = std::stoi(words[i*2]);
            clause.insert(var);
            coefs.insert (pair<Int,Int>(var, coef));
          }
          std::string comparator_string = words[words.size()-3];
          int comparator = 0;
          int k = std::stoi(words[words.size() -2]);
          if(comparator_string == ">=") comparator = 1;
          else if(comparator_string == "=") comparator = 2;
          else if(comparator_string == "<=") comparator = 3;
          PB_canonicalize(clause, coefs, &k, &comparator); // comparator: >= (1), =(2)
          addClause(clause, type, weight, comparator, coefs, k);
        }
        else{
          for (Int i = 0; i < words.size(); i++) {
            if ( words.at(i) == "x"){  // this constraint is an XOR
              type = 'x';
              continue;
            }
            Int num = stoll(words.at(i));

            if (num > declaredVarCount || num < -declaredVarCount) {
              throw MyError("literal '", num, "' inconsistent with declared var count '", declaredVarCount, "' | line ", lineIndex);
            }

            if (num == 0) {
              if (i != words.size() - 1) {
                throw MyError("clause terminated prematurely by '0' | line ", lineIndex);
              }

              if (clause.empty()) {
                throw EmptyClauseException(lineIndex, line);
              }
              addClause(clause, type, weight);
              processedClauseCount++;
            }
            else { // literal
              if (i == words.size() - 1) {
                throw MyError("missing end-of-clause indicator '0' | line ", lineIndex);
              }
              clause.insert(num);
            }
          }
        }
      }
      else{     // typical DIMACS file
        Clause clause;
        if  ((words.front().starts_with("[")) || ((words.at(1).starts_with("x")))){  // WBO/PBO constraint
          type = 'p';
          Map<Int, Int> coefs;
          if (words.front().starts_with("[")){  // soft constraint
            string firstWord = words.at(0);
            weight = stod( firstWord.substr(1,firstWord.length() -2 ));
            words.erase(words.begin());
          }
          else{
            weight = trivialBoundPartialMaxSAT + 1; // weight of a hard constraint is total weight of soft constraints + 1
          }
          for( int i = 0; i < (words.size() - 3) / 2; i++){
            std::string str = words.at(i*2+1);
            Int var = std::stoll(str.substr(1,str.length()));
            Int coef = std::stoi(words[i*2]);
            clause.insert(var);
            coefs.insert (pair<Int,Int>(var, coef));
          }
            std::string comparator_string = words[words.size()-3];
            int comparator = 0;
            int k = std::stoi(words[words.size() -2]);
            if(comparator_string == ">=") comparator = 1;
            else if(comparator_string == "=") comparator = 2;
            else if(comparator_string == "<=") comparator = 3;
            PB_canonicalize(clause, coefs, &k, &comparator); // comparator: >= (1), =(2)
            addClause(clause, type, weight, comparator, coefs, k);
        }
        else{
          for (Int i = 0; i < words.size(); i++) {
            if ( words.at(i) == "x"){  // this constraint is an XOR
              type = 'x';
              continue;
            }
            if ((wcnfFlag && (type == 'c') && (i == 0)) || (wcnfFlag && (type == 'x' ) && (i == 1)) ){  // catch the weight for weighted MaxSAT
              weight = stod(words.at(i));
              continue;
            }
            Int num = stoll(words.at(i));
            if (num > declaredVarCount || num < -declaredVarCount) {
              throw MyError("literal '", num, "' inconsistent with declared var count '", declaredVarCount, "' | line ", lineIndex);
            }

            if (num == 0) {
              if (i != words.size() - 1) {
                throw MyError("clause terminated prematurely by '0' | line ", lineIndex);
              }

              if (clause.empty()) {
                throw EmptyClauseException(lineIndex, line);
              }

              addClause(clause, type, weight);
              processedClauseCount++;
            }
            else { // literal
              if (i == words.size() - 1) {
                throw MyError("missing end-of-clause indicator '0' | line ", lineIndex);
              }
              clause.insert(num);
            }
          }
        }
      }
    }
  }

  if (problemLineIndex == MIN_INT) {
    throw MyError("no problem line before cnf file ends on line ", lineIndex);
  }

  setApparentVars();

  if ( (!projectedCounting) && (!maxsatSolving) ) {  // for maxsat problem, all variables are not additive  (min) variables by default
    for (Int var = 1; var <= declaredVarCount; var++) {
      additiveVars.insert(var);
    }
  }

  if (!weightedCounting) { // populates literalWeights with 1s
    for (Int var = 1; var <= declaredVarCount; var++) {
      literalWeights[var] = Number("1");
      literalWeights[-var] = Number("1");
    }
  }
  else { // completes literalWeights
    for (Int var = 1; var <= declaredVarCount; var++) {
      if (!literalWeights.contains(var) && !literalWeights.contains(-var)) {
        literalWeights[var] = Number("1");
        literalWeights[-var] = Number("1");
      }
      else if (!literalWeights.contains(var)) {
        if (logCounting) {
          assert(literalWeights.at(-var) <= Number("1"));
        }
        literalWeights[var] = Number("1") - literalWeights.at(-var);
      }
      else if (!literalWeights.contains(-var)) {
        if (logCounting) {
          assert(literalWeights.at(var) <= Number("1"));
        }
        literalWeights[-var] = Number("1") - literalWeights.at(var);
      }
    }
  }

  if (verboseCnf >= PARSED_INPUT) {
    util::printRow("declaredVarCount", declaredVarCount);
    util::printRow("apparentVarCount", apparentVars.size());

    util::printRow("declaredClauseCount", declaredClauseCount);
    util::printRow("apparentClauseCount", processedClauseCount);

    if (projectedCounting) {
      cout << "c additive vars: { ";
      for (Int var : std::set<Int>(additiveVars.begin(), additiveVars.end())) {
        cout << var << " ";
      }
      cout << "}\n";
    }

    if (weightedCounting) {
      printLiteralWeights();
    }

    printClauses();
  }

  cout << "\n";
}

/* classes for join trees =================================================== */

/* class Assignment ========================================================= */

Assignment::Assignment() {}

Assignment::Assignment(Int var, bool val) {
  insert({var, val});
}

void Assignment::printAssignment() const {
  for (auto it = begin(); it != end(); it++) {
    cout << right << setw(5) << (it->second ? it->first : -it->first);
    if (next(it) != end()) {
      cout << " ";
    }
  }
}

vector<Assignment> Assignment::extendAssignments(const vector<Assignment>& assignments, Int var) {
  vector<Assignment> extendedAssignments;
  if (assignments.empty()) {
    extendedAssignments.push_back(Assignment(var, false));
    extendedAssignments.push_back(Assignment(var, true));
  }
  else {
    for (Assignment assignment : assignments) {
      assignment[var] = false;
      extendedAssignments.push_back(assignment);
      assignment[var] = true;
      extendedAssignments.push_back(assignment);
    }
  }
  return extendedAssignments;
}

/* class JoinNode =========================================================== */

Int JoinNode::nodeCount;
Int JoinNode::terminalCount;
Set<Int> JoinNode::nonterminalIndices;

Int JoinNode::backupNodeCount;
Int JoinNode::backupTerminalCount;
Set<Int> JoinNode::backupNonterminalIndices;

Cnf JoinNode::cnf;

void JoinNode::resetStaticFields() {
  backupNodeCount = nodeCount;
  backupTerminalCount = terminalCount;
  backupNonterminalIndices = nonterminalIndices;

  nodeCount = 0;
  terminalCount = 0;
  nonterminalIndices.clear();
}

void JoinNode::restoreStaticFields() {
  nodeCount = backupNodeCount;
  terminalCount = backupTerminalCount;
  nonterminalIndices = backupNonterminalIndices;
}

Set<Int> JoinNode::getPostProjectionVars() const {
  return util::getDiff(preProjectionVars, projectionVars);
}

Int JoinNode::chooseClusterIndex(Int clusterIndex, const vector<Set<Int>>& projectableVarSets, string clusteringHeuristic) {
  if (clusterIndex < 0 || clusterIndex >= projectableVarSets.size()) {
    throw MyError("clusterIndex == ", clusterIndex, " whereas projectableVarSets.size() == ", projectableVarSets.size());
  }

  Set<Int> projectableVars = util::getUnion(projectableVarSets); // Z = Z_1 \cup .. \cup Z_m
  Set<Int> postProjectionVars = getPostProjectionVars(); // of this node
  if (util::isDisjoint(projectableVars, postProjectionVars)) {
    return projectableVarSets.size(); // special cluster
  }

  if (clusteringHeuristic == BUCKET_LIST || clusteringHeuristic == BOUQUET_LIST) {
    return clusterIndex + 1;
  }
  for (Int target = clusterIndex + 1; target < projectableVarSets.size(); target++) {
    if (!util::isDisjoint(postProjectionVars, projectableVarSets.at(target))) {
      return target;
    }
  }
  return projectableVarSets.size();
}

Int JoinNode::getNodeRank(const vector<Int>& restrictedVarOrder, string clusteringHeuristic) {
  Set<Int> postProjectionVars = getPostProjectionVars();

  if (clusteringHeuristic == BUCKET_LIST || clusteringHeuristic == BUCKET_TREE) { // min var rank
    Int rank = MAX_INT;
    for (Int varRank = 0; varRank < restrictedVarOrder.size(); varRank++) {
      if (postProjectionVars.contains(restrictedVarOrder.at(varRank))) {
        rank = min(rank, varRank);
      }
    }
    return (rank == MAX_INT) ? restrictedVarOrder.size() : rank;
  }

  Int rank = MIN_INT;
  for (Int varRank = 0; varRank < restrictedVarOrder.size(); varRank++) {
    if (postProjectionVars.contains(restrictedVarOrder.at(varRank))) {
      rank = max(rank, varRank);
    }
  }
  return (rank == MIN_INT) ? restrictedVarOrder.size() : rank;
}

bool JoinNode::isTerminal() const {
  return nodeIndex < terminalCount;
}

/* class JoinTerminal ======================================================= */

Int JoinTerminal::getWidth(const Assignment& assignment) const {
  return util::getDiff(preProjectionVars, assignment).size();
}

void JoinTerminal::updateVarSizes(Map<Int, size_t>& varSizes) const {
  Set<Int> vars = cnf.clauses.at(nodeIndex).getClauseVars();
  for (Int var : vars) {
    varSizes[var] = max(varSizes[var], vars.size());
  }
}

JoinTerminal::JoinTerminal() {
  nodeIndex = terminalCount;
  terminalCount++;
  nodeCount++;

  preProjectionVars = cnf.clauses.at(nodeIndex).getClauseVars();
}

/* class JoinNonterminal ===================================================== */

void JoinNonterminal::printNode(string startWord) const {
  cout << startWord << nodeIndex + 1 << " ";

  for (const JoinNode* child : children) {
    cout << child->nodeIndex + 1 << " ";
  }

  cout << VAR_ELIM_WORD;
  for (Int var : projectionVars) {
    cout << " " << var;
  }

  cout << "\n";
}

void JoinNonterminal::printSubtree(string startWord) const {
  for (const JoinNode* child : children) {
    if (!child->isTerminal()) {
      static_cast<const JoinNonterminal*>(child)->printSubtree(startWord);
    }
  }
  printNode(startWord);
}

Int JoinNonterminal::getWidth(const Assignment& assignment) const {
  Int width = util::getDiff(preProjectionVars, assignment).size();
  for (JoinNode* child : children) {
    width = max(width, child->getWidth(assignment));
  }
  return width;
}

void JoinNonterminal::updateVarSizes(Map<Int, size_t>& varSizes) const {
  for (Int var : preProjectionVars) {
    varSizes[var] = max(varSizes[var], preProjectionVars.size());
  }
  for (JoinNode* child : children) {
    child->updateVarSizes(varSizes);
  }
}

vector<Int> JoinNonterminal::getBiggestNodeVarOrder() const {
  Map<Int, size_t> varSizes; // var x |-> size of biggest node containing x
  for (Int var : cnf.apparentVars) {
    varSizes[var] = 0;
  }

  updateVarSizes(varSizes);

  multimap<size_t, Int, greater<size_t>> sizedVars = util::flipMap(varSizes); // size |-> var

  size_t prevSize = 0;

  if (verboseSolving >= 2) {
    cout << THIN_LINE;
  }

  vector<Int> varOrder;
  for (auto [varSize, var] : sizedVars) {
    varOrder.push_back(var);

    if (verboseSolving >= 2) {
      if (prevSize == varSize) {
        cout << " " << var;
      }
      else {
        if (prevSize > 0) {
          cout << "\n";
        }
        prevSize = varSize;
        cout << "c vars in nodes of size " << right << setw(5) << varSize << ": " << var;
      }
    }
  }

  if (verboseSolving >= 2) {
    cout << "\n" << THIN_LINE;
  }

  return varOrder;
}

vector<Int> JoinNonterminal::getHighestNodeVarOrder() const {
  vector<Int> varOrder;
  std::queue<const JoinNonterminal*> q;
  q.push(this);
  while (!q.empty()) {
    const JoinNonterminal* n = q.front();
    q.pop();
    for (Int var : n->projectionVars) {
      varOrder.push_back(var);
    }
    for (const JoinNode* child : n->children) {
      if (!child->isTerminal()) {
        q.push(static_cast<const JoinNonterminal*>(child));
      }
    }
  }
  return varOrder;
}

vector<Int> JoinNonterminal::getVarOrder(Int varOrderHeuristic) const {
  if (CNF_VAR_ORDER_HEURISTICS.contains(abs(varOrderHeuristic))) {
    return cnf.getCnfVarOrder(varOrderHeuristic);
  }

  vector<Int> varOrder;
  if (abs(varOrderHeuristic) == BIGGEST_NODE) {
    varOrder = getBiggestNodeVarOrder();
  }
  else {
    assert(abs(varOrderHeuristic) == HIGHEST_NODE);
    varOrder = getHighestNodeVarOrder();
  }

  if (varOrderHeuristic < 0) {
    reverse(varOrder.begin(), varOrder.end());
  }

  return varOrder;
}

vector<Assignment> JoinNonterminal::getAdditiveAssignments(Int varOrderHeuristic, Int sliceVarCount) const {
  if (sliceVarCount <= 0) {
    return vector<Assignment>{Assignment()};
  }

  TimePoint sliceVarOrderStartPoint = util::getTimePoint();
  vector<Int> varOrder = getVarOrder(varOrderHeuristic);
  if (verboseSolving >= 1) {
    util::printRow("sliceVarSeconds", util::getDuration(sliceVarOrderStartPoint));
  }

  TimePoint assignmentsStartPoint = util::getTimePoint();
  vector<Assignment> assignments;

  if (verboseSolving >= 2) {
    cout << "c slice var order: {";
  }

  for (Int i = 0, assignedVars = 0; i < varOrder.size() && assignedVars < sliceVarCount; i++) {
    Int var = varOrder.at(i);
    if (cnf.additiveVars.contains(var)) {
      assignments = Assignment::extendAssignments(assignments, var);
      assignedVars++;
      if (verboseSolving >= 2) {
        cout << " " << var;
      }
    }
  }

  if (verboseSolving >= 2) {
    cout << " }\n";
  }

  if (verboseSolving >= 1) {
    util::printRow("sliceAssignmentsSeconds", util::getDuration(assignmentsStartPoint));
  }

  return assignments;
}

JoinNonterminal::JoinNonterminal(const vector<JoinNode*>& children, const Set<Int>& projectionVars, Int requestedNodeIndex) {
  this->children = children;
  this->projectionVars = projectionVars;

  if (requestedNodeIndex == MIN_INT) {
    requestedNodeIndex = nodeCount;
  }
  else if (requestedNodeIndex < terminalCount) {
    throw MyError("requestedNodeIndex == ", requestedNodeIndex, " < ", terminalCount, " == terminalCount");
  }
  else if (nonterminalIndices.contains(requestedNodeIndex)) {
    throw MyError("requestedNodeIndex ", requestedNodeIndex, " already taken");
  }

  nodeIndex = requestedNodeIndex;
  nonterminalIndices.insert(nodeIndex);
  nodeCount++;

  for (JoinNode* child : children) {
    util::unionize(preProjectionVars, child->getPostProjectionVars());
  }
}

/* global functions ========================================================= */

ostream& operator<<(ostream& stream, const Number& n) {
  if (multiplePrecision) {
    stream << n.quotient;
  }
  else {
    stream << n.fraction;
  }

  return stream;
}
