/*=============================================================================
# Filename: Strategy.cpp
# Author: Bookug Lobert
# Mail: zengli-bookug@pku.edu.cn
# Last Modified: 2016-05-07 16:31
# Description: implement functions in Strategy.h
=============================================================================*/

#include "Strategy.h"

using namespace std;

Strategy::Strategy()
{
	this->method = 0;
	this->kvstore = NULL;
	this->vstree = NULL;
	//this->prepare_handler();
}

Strategy::Strategy(KVstore* _kvstore, VSTree* _vstree, TNUM* _pre2num, int _limitID_predicate, int _limitID_literal)
{
	this->method = 0;
	this->kvstore = _kvstore;
	this->vstree = _vstree;
	this->pre2num = _pre2num;
	this->limitID_predicate = _limitID_predicate;
	this->limitID_literal = _limitID_literal;
	//this->prepare_handler();
}

Strategy::~Strategy()
{
	//delete[] this->dispatch;
}

//void
//Strategy::prepare_handler()
//{
//this->dispatch = new QueryHandler[Strategy::QUERY_HANDLER_NUM];
//this->dispatch[0] = Strategy::handler0;
//}

//NOTICE: 2-triple case ?s1 p1 c0 ?s2 p2 c0 is viewed as an unconnected graph
//however, this can be dealed due to several basic queries and linking

bool
Strategy::handle(SPARQLquery& _query, ResultFilter* _result_filter)
{
#ifdef MULTI_INDEX
	Util::logging("IN GeneralEvaluation::handle");

	vector<BasicQuery*>& queryList = _query.getBasicQueryVec();
	// enumerate each BasicQuery and retrieve their variables' mapping entity in the VSTree.
	vector<BasicQuery*>::iterator iter = queryList.begin();
	for (; iter != queryList.end(); iter++)
	if ((**iter).getEncodeBasicQueryResult())
	{
		this->method = -1;

		vector<int*>& result_list = (*iter)->getResultList();
		//int select_var_num = (*iter)->getSelectVarNum();
		//the num of vars needing to be joined, i.e. selectVarNum if only one triple
		int varNum = (*iter)->getVarNum();  
		//all variables(not including pre vars)
		int total_num = (*iter)->getTotalVarNum();
		int pre_varNum = (*iter)->getPreVarNum();
		int selected_pre_var_num = (*iter)->getSelectedPreVarNum();
		int selected_var_num = (*iter)->getSelectVarNum();

		//NOTICE: special case - query vertices only connected via same variables
		//all constant triples will be viewed as unconnected, if a triple has no variable, 
		//then this triple is a BGP(no other triples in this BGP)
		if(total_num == 0 && pre_varNum == 0)
		{
			this->method = 5;
		}
		else if ((*iter)->getTripleNum() == 1 && pre_varNum == 1)
		{
			this->method = 4;
		}

		if (this->method < 0 && pre_varNum == 0 && (*iter)->getTripleNum() == 1)    //only one triple and no predicates
		{
			//only one variable and one triple: ?s pre obj or sub pre ?o
			if (total_num == 1)
			{
				this->method = 1;
			}
			//only two vars: ?s pre ?o
			else if (total_num == 2)
			{
				if (varNum == 1)   //the selected id should be 0
				{
					this->method = 2;
				}
				else   //==2
				{
					this->method = 3;
				}
			}
			//cout << "this BasicQuery use query strategy 2" << endl;
			//cout<<"Final result size: "<<(*iter)->getResultList().size()<<endl;
			//continue;
		}
		if(this->method< 0)
		{
			this->method = 0;
		}

		//QueryHandler dispatch;
		//dispatch[0] = handler0;
		switch (this->method)
		{
		//BETTER: use function pointer array in C++ class
		case 0:
			//default:filter by vstree and then verified by join
			this->handler0(*iter, result_list, _result_filter);
			break;
		case 1:
			this->handler1(*iter, result_list);
			break;
		case 2:
			this->handler2(*iter, result_list);
			break;
		case 3:
			this->handler3(*iter, result_list);
			break;
		case 4:
			this->handler4(*iter, result_list);
			break;
		case 5:
			this->handler5(*iter, result_list);
			break;
		default:
			cout << "not support this method" << endl;

		}
		cout << "BasicQuery -- Final result size: " << (*iter)->getResultList().size() << endl;
	}
#else
	cout << "this BasicQuery use original query strategy" << endl;
	long tv_handle = Util::get_cur_time();
	(this->vstree)->retrieve(_query);
	long tv_retrieve = Util::get_cur_time();
	cout << "after Retrieve, used " << (tv_retrieve - tv_handle) << "ms." << endl;

	this->join = new Join(kvstore, pre2num, this->limitID_predicate, this->limitID_literal);
	this->join->join_sparql(_query);
	delete this->join;

	long tv_join = Util::get_cur_time();
	cout << "after Join, used " << (tv_join - tv_retrieve) << "ms." << endl;
#endif
	Util::logging("OUT Strategy::handle");
	return true;
}

void
Strategy::handler0(BasicQuery* _bq, vector<int*>& _result_list, ResultFilter* _result_filter)
{
	//long before_filter = Util::get_cur_time();
	cout << "this BasicQuery use query strategy 0" << endl;

	//BETTER:not all vars in join filtered by vstree
	//(A)-B-c: B should by vstree, then by c, but A should be generated in join(first set A as not)
	//if A not in join, just filter B by pre
	//divided into star graphs, join core vertices, generate satellites
	//join should also start from a core vertex(neighbor can be constants or vars) if available
	//
	//QUERY: is there any case that a node should be retrieved by other index?(instead of vstree or generate whne join)
	//
	//we had better treat 1-triple case(no ?p) as special, and then in other cases, core vertex exist(if connected)
	//However, if containing ?p and 1-triple, we should treat it also as a special case, or select a variable as core vertex
	//and retrieved (for example, ?s ?p o   or    s ?p ?o, generally no core vertex in these cases)

	long tv_handle = Util::get_cur_time();
	int varNum = _bq->getVarNum();  //the num of vars needing to be joined
	//TODO:parallel by pthread
	for (int i = 0; i < varNum; ++i)
	{
		if (_bq->if_need_retrieve(i) == false)
			continue;
		bool flag = _bq->isLiteralVariable(i);
		const EntityBitSet& entityBitSet = _bq->getVarBitSet(i);
		IDList* idListPtr = &(_bq->getCandidateList(i));
		this->vstree->retrieveEntity(entityBitSet, idListPtr);
		if (!flag)
		{
			//cout<<"set ready: "<<i<<endl;
			_bq->setReady(i);
		}
		//the basic query should end if one non-literal var has no candidates
		if (idListPtr->size() == 0 && !flag)
		{
			break;
		}
	}

	//BETTER:end directly if one is empty!

	long tv_retrieve = Util::get_cur_time();
	cout << "after Retrieve, used " << (tv_retrieve - tv_handle) << "ms." << endl;

    //between retrieve and join
    if (_result_filter != NULL)
    	_result_filter->candFilterWithResultHashTable(*_bq);

	Join *join = new Join(kvstore, pre2num, this->limitID_predicate, this->limitID_literal);
	join->join_basic(_bq);
	delete join;

	long tv_join = Util::get_cur_time();
	cout << "after Join, used " << (tv_join - tv_retrieve) << "ms." << endl;
}

void
Strategy::handler1(BasicQuery* _bq, vector<int*>& _result_list)
{
	long before_filter = Util::get_cur_time();
	cout << "this BasicQuery use query strategy 1" << endl;
	//int neighbor_id = (*_bq->getEdgeNeighborID(0, 0);  //constant, -1
	char edge_type = _bq->getEdgeType(0, 0);
	int triple_id = _bq->getEdgeID(0, 0);
	Triple triple = _bq->getTriple(triple_id);
	int pre_id = _bq->getEdgePreID(0, 0);
	int* id_list = NULL;
	int id_list_len = 0;
	if (edge_type == Util::EDGE_OUT)
	{
		//cout<<"edge out!!!"<<endl;
		int nid = (this->kvstore)->getIDByEntity(triple.object);
		if (nid == -1)
		{
			nid = (this->kvstore)->getIDByLiteral(triple.object);
		}
		this->kvstore->getsubIDlistByobjIDpreID(nid, pre_id, id_list, id_list_len);
	}
	else
	{
		//cout<<"edge in!!!"<<endl;
		this->kvstore->getobjIDlistBysubIDpreID(this->kvstore->getIDByEntity(triple.subject), pre_id, id_list, id_list_len);
	}

	long after_filter = Util::get_cur_time();
	cout << "after filter, used " << (after_filter - before_filter) << "ms" << endl;
	_result_list.clear();
	//cout<<"now to copy result to list"<<endl;
	for (int i = 0; i < id_list_len; ++i)
	{
		int* record = new int[1];    //only this var is selected
		record[0] = id_list[i];
		//cout<<this->kvstore->getEntityByID(record[0])<<endl;
		_result_list.push_back(record);
	}
	long after_copy = Util::get_cur_time();
	cout << "after copy to result list: used " << (after_copy - after_filter) << " ms" << endl;
	delete[] id_list;
	cout << "Final result size: " << _result_list.size() << endl;
}

void
Strategy::handler2(BasicQuery* _bq, vector<int*>& _result_list)
{
	long before_filter = Util::get_cur_time();
	cout << "this BasicQuery use query strategy 2" << endl;
	int triple_id = _bq->getEdgeID(0, 0);
	Triple triple = _bq->getTriple(triple_id);
	int pre_id = _bq->getEdgePreID(0, 0);

	//NOTICE:it is ok for var1 or var2 to be -1, i.e. not encoded
	int var1_id = _bq->getIDByVarName(triple.subject);
	int var2_id = _bq->getIDByVarName(triple.object);

	int* id_list = NULL;
	int id_list_len = 0;
	if (var1_id == 0)   //subject var selected
	{
		//use p2s directly
		this->kvstore->getsubIDlistBypreID(pre_id, id_list, id_list_len);
	}
	else if (var2_id == 0) //object var selected
	{
		//use p2o directly
		this->kvstore->getobjIDlistBypreID(pre_id, id_list, id_list_len);
	}
	else
	{
		cout << "ERROR in Database::handle(): no selected var!" << endl;
	}
	long after_filter = Util::get_cur_time();
	cout << "after filter, used " << (after_filter - before_filter) << "ms" << endl;
	_result_list.clear();
	for (int i = 0; i < id_list_len; ++i)
	{
		int* record = new int[1];    //only one var
		record[0] = id_list[i];
		_result_list.push_back(record);
	}
	long after_copy = Util::get_cur_time();
	cout << "after copy to result list: used " << (after_copy - after_filter) << " ms" << endl;
	delete[] id_list;
	cout << "Final result size: " << _result_list.size() << endl;
}

void
Strategy::handler3(BasicQuery* _bq, vector<int*>& _result_list)
{
	long before_filter = Util::get_cur_time();
	cout << "this BasicQuery use query strategy 3" << endl;
	int triple_id = _bq->getEdgeID(0, 0);
	Triple triple = _bq->getTriple(triple_id);
	int pre_id = _bq->getEdgePreID(0, 0);
	int* id_list = NULL;
	int id_list_len = 0;

	_result_list.clear();
	this->kvstore->getsubIDobjIDlistBypreID(pre_id, id_list, id_list_len);
	int var1_id = _bq->getSelectedVarPosition(triple.subject);
	int var2_id = _bq->getSelectedVarPosition(triple.object);

	if(var1_id < 0 || var2_id < 0)
	{
		delete[] id_list;
		return;
	}

	long after_filter = Util::get_cur_time();
	cout << "after filter, used " << (after_filter - before_filter) << "ms" << endl;

	for (int i = 0; i < id_list_len; i += 2)
	{
		int* record = new int[2];    //2 vars and selected
		record[var1_id] = id_list[i];
		record[var2_id] = id_list[i + 1];
		_result_list.push_back(record);
	}

	long after_copy = Util::get_cur_time();
	cout << "after copy to result list: used " << (after_copy - after_filter) << " ms" << endl;
	delete[] id_list;
	cout << "Final result size: " << _result_list.size() << endl;
}

void
Strategy::handler4(BasicQuery* _bq, vector<int*>& _result_list)
{
	cout<<"Special Case: consider pre var in this triple"<<endl;
	int varNum = _bq->getVarNum();  
	//all variables(not including pre vars)
	int total_num = _bq->getTotalVarNum();
	int pre_varNum = _bq->getPreVarNum();
	int selected_pre_var_num = _bq->getSelectedPreVarNum();
	int selected_var_num = _bq->getSelectVarNum();
	Triple triple = _bq->getTriple(0);
	int pvpos = _bq->getSelectedPreVarPosition(triple.predicate);
	int* id_list = NULL;
	int id_list_len = 0;
	_result_list.clear();

	//cout<<"total num: "<<total_num <<endl;
	if (total_num == 2)
	{
		cout<<"Special Case 1"<<endl;
		int svpos = _bq->getSelectedVarPosition(triple.subject);
		int ovpos = _bq->getSelectedVarPosition(triple.object);
		cout<<"subject: "<<triple.subject<<" "<<svpos<<endl;
		cout<<"object: "<<triple.object<<" "<<ovpos<<endl;
		cout<<"predicate: "<<triple.predicate<<" "<<pvpos<<endl;
		//very special case, to find all triples, select ?s (?p) ?o where { ?s ?p ?o . }
		//filter and join is too costly, should enum all predicates and use p2so
		for(int i = 0; i < this->limitID_predicate; ++i)
		{
			int pid = i;
			this->kvstore->getsubIDobjIDlistBypreID(pid, id_list, id_list_len);
			int rsize = selected_var_num;
			if(selected_pre_var_num == 1)
			{
				rsize++;
			}

			//always place s/o before p in result list
			for (int j = 0; j < id_list_len; j += 2)
			{
				int* record = new int[rsize];
				//check the s/o var if selected, need to ensure the placement order
				if(ovpos >= 0)
				{
					record[ovpos] = id_list[j+1];
				}
				if(svpos >= 0)
				{
					record[svpos] = id_list[j];
				}

				if(pvpos >= 0)
				{
					record[pvpos] = pid;      //for the pre var
				}
				_result_list.push_back(record);
			}
			delete[] id_list;
		}
		id_list = NULL;
	}
	else if (total_num == 1)
	{
		cout<<"Special Case 2"<<endl;
		int vpos = -1;
		if(triple.subject[0] != '?')  //constant
		{
			int sid = (this->kvstore)->getIDByEntity(triple.subject);
			this->kvstore->getpreIDobjIDlistBysubID(sid, id_list, id_list_len);
			vpos = _bq->getSelectedVarPosition(triple.object);
		}
		else if(triple.object[0] != '?')  //constant
		{
			int oid = (this->kvstore)->getIDByEntity(triple.object);
			if (oid == -1)
			{
				oid = (this->kvstore)->getIDByLiteral(triple.object);
			}
			this->kvstore->getpreIDsubIDlistByobjID(oid, id_list, id_list_len);
			vpos = _bq->getSelectedVarPosition(triple.subject);
		}

		int rsize = varNum;
		if(selected_pre_var_num == 1)
		{
			rsize++;
		}
		//always place s/o before p in result list
		for (int i = 0; i < id_list_len; i += 2)
		{
			int* record = new int[rsize];
			if(vpos >= 0)
			{
				record[vpos] = id_list[i + 1]; //for the s/o var
			}
			if(pvpos >= 0)
			{
				record[pvpos] = id_list[i];      //for the pre var
			}
			_result_list.push_back(record);
		}
	}
	else if (total_num == 0)  //only ?p and it must be selected
	{
		cout<<"Special Case 3"<<endl;
		//just use so2p
		int sid = (this->kvstore)->getIDByEntity(triple.subject);
		int oid = (this->kvstore)->getIDByEntity(triple.object);
		if (oid == -1)
		{
			oid = (this->kvstore)->getIDByLiteral(triple.object);
		}

		this->kvstore->getpreIDlistBysubIDobjID(sid, oid, id_list, id_list_len);
		//copy to result list
		for (int i = 0; i < id_list_len; ++i)
		{
			int* record = new int[1];
			record[0] = id_list[i];
			_result_list.push_back(record);
		}
	}

	delete[] id_list;
}

//TODO:if any constants in a query are not found in kvstore, then this BGP should end to speed up the processing

void
Strategy::handler5(BasicQuery* _bq, vector<int*>& _result_list)
{
	cout<<"Special Case: consider constant triple"<<endl;
	Triple triple = _bq->getTriple(0);
	_result_list.clear();

	int subid = this->kvstore->getIDByEntity(triple.subject);
	if(subid == -1) //not found
	{
		return;
	}
	int preid = this->kvstore->getIDByPredicate(triple.predicate);
	if(preid == -1) //not found
	{
		return;
	}
	int objid = this->kvstore->getIDByEntity(triple.object);
	if(objid == -1)
	{
		objid = this->kvstore->getIDByLiteral(triple.object);
	}
	if(objid == -1)
	{
		return;
	}

	int* id_list = NULL;
	int id_list_len = 0;
	(this->kvstore)->getobjIDlistBysubIDpreID(subid, preid, id_list, id_list_len);
	if (Util::bsearch_int_uporder(objid, id_list, id_list_len) != -1)
	{
		int* record = new int[3];
		record[0] = subid;
		record[1] = preid;
		record[2] = objid;
		_result_list.push_back(record);
	}
	delete[] id_list;
}

