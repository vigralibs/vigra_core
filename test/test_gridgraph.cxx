/************************************************************************/
/*                                                                      */
/*              Copyright 2012-2013 by Ullrich Koethe                   */
/*                                                                      */
/*    This file is part of the VIGRA computer vision library.           */
/*    The VIGRA Website is                                              */
/*        http://hci.iwr.uni-heidelberg.de/vigra/                       */
/*    Please direct questions, bug reports, and contributions to        */
/*        ullrich.koethe@iwr.uni-heidelberg.de    or                    */
/*        vigra@informatik.uni-hamburg.de                               */
/*                                                                      */
/*    Permission is hereby granted, free of charge, to any person       */
/*    obtaining a copy of this software and associated documentation    */
/*    files (the "Software"), to deal in the Software without           */
/*    restriction, including without limitation the rights to use,      */
/*    copy, modify, merge, publish, distribute, sublicense, and/or      */
/*    sell copies of the Software, and to permit persons to whom the    */
/*    Software is furnished to do so, subject to the following          */
/*    conditions:                                                       */
/*                                                                      */
/*    The above copyright notice and this permission notice shall be    */
/*    included in all copies or substantial portions of the             */
/*    Software.                                                         */
/*                                                                      */
/*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND    */
/*    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES   */
/*    OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND          */
/*    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT       */
/*    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,      */
/*    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING      */
/*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR     */
/*    OTHER DEALINGS IN THE SOFTWARE.                                   */
/*                                                                      */
/************************************************************************/

#define VIGRA_CHECK_BOUNDS
#include "vigra2/unittest.hxx"
#include <vigra2/shape.hxx>
#include <vigra2/iterator_nd.hxx>
#include <vigra2/array_nd.hxx>
//#include <vigra2/gridgraph.hxx>
//#include <vigra2/multi_localminmax.hxx>
//#include <vigra2/algorithm.hxx>

#include <vector>
#include <numeric>

//#ifdef WITH_BOOST_GRAPH
//#  include <boost/graph/graph_concepts.hpp>
//#endif


using namespace vigra;

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif


namespace vigra {

inline ArrayIndex gridGraphMaxDegree(unsigned int N, NeighborhoodType t)
{
    return t == DirectNeighborhood
        ? 2 * N
        : pow(3, (int)N) - 1;
}

template<int N>
class GridGraphArcDescriptor
    : public Shape<(N == runtime_size) ? N : N + 1>
{
public:
    static const int dimension = (N == runtime_size) ? N : N + 1;
    typedef Shape<dimension>                                     base_type;
    typedef typename base_type::value_type                       value_type;
    typedef base_type                                            edge_coord_type;
    typedef value_type                                           index_type;
    typedef Shape<N>                                             shape_type;
    typedef decltype(((base_type*)0)->template subarray<0, 1>())  vertex_descriptor_view;

    GridGraphArcDescriptor()
        : is_reversed_(false)
    {}

    GridGraphArcDescriptor(lemon::Invalid)
        : base_type(tags::size = dimension, -1)
        , is_reversed_(false)
    {}

    GridGraphArcDescriptor(base_type const & b, bool reversed)
        : base_type(b)
        , is_reversed_(reversed)
    {}

    GridGraphArcDescriptor(shape_type const &vertex,
        index_type edge_index,
        bool reversed = false)
        : base_type(tags::size = vertex.size() + 1, DontInit)
        , is_reversed_(reversed)
    {
        for (int k = 0; k < vertex.size(); ++k)
            (*this)[k] = vertex[k];
        this->back() = edge_index;
    }

    void set(shape_type const & vertex, index_type edge_index, bool reversed)
    {
        GridGraphArcDescriptor(vertex, edge_index, reversed).swap(*this);
    }

    void increment(GridGraphArcDescriptor const & diff, bool opposite = false)
    {
        if (diff.is_reversed_)
        {
            is_reversed_ = !opposite;
            for (int k = 0; k < this->size() - 1; ++k)
                (*this)[k] += diff[k];
        }
        else
        {
            is_reversed_ = opposite;
        }
        this->back() = diff.back();
    }

    bool isReversed() const
    {
        return is_reversed_;
    }

    template <int M = N>
    enable_if_t<M != runtime_size, vertex_descriptor_view>
        vertexDescriptor() const
    {
        return this->template subarray<0, N>();
    }

    template <int M = N>
    enable_if_t<M == runtime_size, vertex_descriptor_view>
        vertexDescriptor() const
    {
        return this->subarray(0, this->size() - 1);
    }

    value_type edgeIndex() const
    {
        return this->back();
    }

protected:
    bool is_reversed_;
};

namespace detail {

// Create the list of neighbor offsets for the given neighborhood type
// and dimension (the dimension is implicitly defined by the Shape type)
// an return it in 'neighborOffsets'. Moreover, create a list of flags
// for each BorderType that is 'true' when the corresponding neighbor exists
// in this border situation and return the result in 'neighborExists'.
template <class SHAPE>
void
makeArrayNeighborhood(
    unsigned int ndim, NeighborhoodType neighborhoodType, MemoryOrder order,
    std::vector<SHAPE> & neighborOffsets,
    std::vector<std::vector<bool>> & neighborExists)
{
    neighborOffsets.clear();
    if (neighborhoodType == DirectNeighborhood)
    {
        SHAPE axes = (order == F_ORDER)
            ? reversed(SHAPE::range(ndim))
            : SHAPE::range(ndim);
        for (int k = 0; k < (int)ndim; ++k)
            neighborOffsets.push_back(-SHAPE::unitVector(tags::size = ndim, axes[k]));
        for (int k = (int)ndim - 1; k >= 0; --k)
            neighborOffsets.push_back(SHAPE::unitVector(tags::size = ndim, axes[k]));
    }
    else
    {
        CoordinateIterator<SHAPE::static_size> c(SHAPE(tags::size = ndim, 3), order);
        for (; c.isValid(); ++c)
        {
            if (*c == 1)
                continue;
            neighborOffsets.push_back(*c - 1);
        }
    }

    unsigned int borderTypeCount = 1 << 2 * ndim,
                 degree = gridGraphMaxDegree(ndim, neighborhoodType);
    neighborExists.clear();
    for (unsigned int bt = 0; bt < borderTypeCount; ++bt)
    {
        std::vector<bool> exists(degree);
        for (unsigned int o = 0; o < degree; ++o)
        {
            bool inside = true;
            for (unsigned int k = 0; k < ndim; ++k)
            {
                if ((neighborOffsets[o][k] < 0 && (bt & (1 << 2 * k)) != 0) ||
                    (neighborOffsets[o][k] > 0 && (bt & (2 << 2 * k)) != 0))
                {
                    inside = false;
                    break;
                }
            }
            exists[o] = inside;
        }
        neighborExists.emplace_back(std::move(exists));
    }
}

template <class SHAPE>
void
computeNeighborIncrements(
    std::vector<SHAPE> const & neighborOffsets,
    std::vector<std::vector<bool> > const & neighborExists,
    std::vector<std::vector<SHAPE> > & adjacentNodeIncrements,
    std::vector<std::vector<GridGraphArcDescriptor<SHAPE::static_size>>> & adjacentArcIncrements,
    std::vector<std::vector<ArrayIndex> > & indices,
    std::vector<std::vector<ArrayIndex> > & backIndices,
    bool directed)
{
    typedef GridGraphArcDescriptor<SHAPE::static_size> ArcDescriptor;

    unsigned int borderTypeCount = neighborExists.size();
    adjacentNodeIncrements.resize(borderTypeCount);
    adjacentArcIncrements.resize(borderTypeCount);
    indices.resize(borderTypeCount);
    backIndices.resize(borderTypeCount);

    for (unsigned int bt = 0; bt<borderTypeCount; ++bt)
    {
        adjacentNodeIncrements[bt].clear();
        adjacentArcIncrements[bt].clear();
        indices[bt].clear();
        backIndices[bt].clear();

        for (unsigned int j = 0; j < neighborOffsets.size(); ++j)
        {
            if (neighborExists[bt][j])
            {
                if (adjacentNodeIncrements[bt].size() == 0)
                {
                    adjacentNodeIncrements[bt].push_back(neighborOffsets[j]);
                }
                else
                {
                    adjacentNodeIncrements[bt].push_back(neighborOffsets[j] - neighborOffsets[indices[bt].back()]);
                }

                if (directed || j < neighborOffsets.size() / 2) // directed or backward edge
                {
                    adjacentArcIncrements[bt].push_back(ArcDescriptor(Shape(), j));
                }
                else if (adjacentArcIncrements[bt].size() == 0 || !adjacentArcIncrements[bt].back().isReversed()) // the first forward edge
                {
                    adjacentArcIncrements[bt].push_back(ArcDescriptor(neighborOffsets[j], neighborOffsets.size() - j - 1, true));
                }
                else // second or higher forward edge
                {
                    adjacentArcIncrements[bt].push_back(ArcDescriptor(neighborOffsets[j] - neighborOffsets[indices[bt].back()],
                        neighborOffsets.size() - j - 1, true));
                }

                indices[bt].push_back(j);
                if (j < neighborOffsets.size() / 2)
                    backIndices[bt].push_back(j);
            }
        }
    }
}

} // namespace detail

} // namespace vigra

template <int N>
struct NeighborhoodTests
{
    typedef Shape<N> S;

    static const int ndim = (N == runtime_size)
        ? 3
        : N;
    
    std::vector<S> neighborOffsets;
    std::vector<std::vector<bool>> neighborExists;
    std::vector<std::vector<S> > relativeOffsets, backOffsets, forwardOffsets;
    //std::vector<std::vector<GridGraphArcDescriptor<N> > > edgeDescrOffsets, backEdgeDescrOffsets, forwardEdgeDescrOffsets;
    std::vector<std::vector<ArrayIndex> > neighborIndices, backIndices, forwardIndices;

    NeighborhoodTests()
    {}
    
    void testVertexIterator()
    {
        CoordinateIterator<N> i(S(tags::size=ndim, 3)), iend = i.end();
        
        for(; i != iend; ++i)
        {
            // create all possible array shapes from 1**N to 3**N
            S s = *i + 1;
            CoordinateIterator<N> vi(s), viend = vi.end();
            
            ArrayND<N, int> vertex_map(s);

            for(; vi != viend; ++vi)
            {
                should(vi.isValid() && !vi.atEnd());
                vertex_map[*vi] += 1;
            }
            
            should(!vi.isValid() && vi.atEnd());
            
            // check that all vertice are found
            auto minmax = vertex_map.minmax();
            
            shouldEqual(minmax[0], 1);
            shouldEqual(minmax[1], 1);
        }
    }   

    void testNeighborhoodImpl(NeighborhoodType neighborhoodType, MemoryOrder memoryOrder)
    {
        detail::makeArrayNeighborhood(ndim, neighborhoodType, memoryOrder, neighborOffsets, neighborExists);

        const int neighborCount = (neighborhoodType == DirectNeighborhood)
                                            ? 2 * ndim
                                            : pow(3, ndim) - 1;
        shouldEqual(neighborOffsets.size(), neighborCount);
        shouldEqual(neighborExists.size(), pow(2, 2*ndim));
        shouldEqual(gridGraphMaxDegree(ndim, neighborhoodType), neighborCount);

        ArrayND<ndim, int> scanOrder(S(tags::size = ndim, 3), memoryOrder);
        std::iota(scanOrder.begin(), scanOrder.end(), 1);
        scanOrder[scanOrder.shape() / 2] = 0;

        int scanOrderIndex = 0;
        S forward(tags::size = ndim), backward(tags::size = ndim), 
          strides = shapeToStrides(scanOrder.shape(), memoryOrder);
        for (int k = 0; k<neighborCount; ++k)
        {
            // test that neighbors are listed in scan order
            should(scanOrderIndex < scanOrder[neighborOffsets[k] + 1]);
            scanOrderIndex = scanOrder[neighborOffsets[k] + 1];
            shouldEqual(dot(strides, neighborOffsets[k]), scanOrderIndex - scanOrder.size() / 2 - 1);

            if (neighborhoodType == DirectNeighborhood)
            {
                // test that offset is +-1 in exactly one direction
                shouldEqual(sum(abs(neighborOffsets[k])), 1);
            }
            else
            {
                // check that offset is at most +-1 in any direction
                shouldEqual(max(abs(neighborOffsets[k])), 1);
            }

            // mark neighbor as found
            scanOrder[neighborOffsets[k] + 1] = 0;

            if (k < neighborCount / 2)
            {
                should(dot(strides, neighborOffsets[k]) < 0); // check that backward neighbors are first
                backward += neighborOffsets[k];               // register backward neighbors
            }
            else
            {
                should(dot(strides, neighborOffsets[k]) > 0); // check that forward neighbors are last
                forward += neighborOffsets[k];                // register forward neighbors
            }

            shouldEqual(neighborOffsets[k], -neighborOffsets[neighborCount - 1 - k]); // check index of opposite neighbor
        }

        if (neighborhoodType == DirectNeighborhood)
        {
            shouldEqual(backward, S(tags::size = ndim, -1)); // check that all backward neighbors were found
            shouldEqual(forward, S(tags::size = ndim, 1));   // check that all forward neighbors were found
        }
        else
        {
            shouldNot(scanOrder.any()); // check that all neighbors were found
        }

        // check neighborhoods at ROI border
        ArrayND<1, uint8_t> checkNeighborCodes(Shape<1>(neighborExists.size()), (uint8_t)0);
        CoordinateIterator<N> i(S(tags::size = ndim, 3));
        for (; i.isValid(); ++i)
        {
            // create all possible array shapes from 1**N to 3**N
            // and check neighborhood of all pixels
            CoordinateIterator<N> vi(*i + 1);
            for (; vi.isValid(); ++vi)
            {
                int borderType = vi.borderType();

                shouldEqual(neighborExists[borderType].size(), neighborCount);
                checkNeighborCodes[borderType] = 1;

                for (int k = 0; k<neighborCount; ++k)
                {
                    // check that neighbors are correctly marked as inside or outside in neighborExists
                    shouldEqual(vi.isInside(vi.coord() + neighborOffsets[k]), neighborExists[borderType][k]);
                }
            }
        }

        should(checkNeighborCodes.all()); // check that all possible neighborhoods have been tested
    }

    void testDirectNeighborhoodFOrder()
    {
        testNeighborhoodImpl(DirectNeighborhood, F_ORDER);
    }

    void testDirectNeighborhoodCOrder()
    {
        testNeighborhoodImpl(DirectNeighborhood, C_ORDER);
    }

    void testIndirectNeighborhoodFOrder()
    {
        testNeighborhoodImpl(IndirectNeighborhood, F_ORDER);
    }

    void testIndirectNeighborhoodCOrder()
    {
        testNeighborhoodImpl(IndirectNeighborhood, C_ORDER);
    }

#if 0    

    template <NeighborhoodType NType, MemoryOrder Order>
    void testNeighborhoodIterator()
    {
        detail::makeArrayNeighborhood(ndim, NType, Order, neighborOffsets, neighborExists);
        detail::computeNeighborOffsets(neighborOffsets, neighborExists, relativeOffsets, edgeDescrOffsets, neighborIndices, backIndices, true);
        
        // check neighborhoods at ROI border
        CoordinateIterator<N> i(Shape(3)), iend = i.getEndIterator();
        ArrayND<N, int> a(Shape(3));
        typedef typename ArrayND<N, int>::view_type View;
        
        for(; i != iend; ++i)
        {
            // create all possible array shapes from 1**N to 3**N
            View va = a.subarray(Shape(), *i+Shape(1)); 
            
            // check neighborhood of all pixels
            typename View::iterator vi = va.begin(), viend = vi.getEndIterator();
            for(; vi != viend; ++vi)
            {
                int borderType = vi.borderType();
                
                {
                    GridGraphNeighborIterator<N> ni(relativeOffsets[borderType], 
                                                    neighborIndices[borderType], 
                                                    backIndices[borderType], 
                                                    vi.point()),
                                                 nend = ni.getEndIterator();
                    
                    for(int k=0; k<neighborExists[borderType].size(); ++k)
                    {
                        if(neighborExists[borderType][k])
                        {
                            should(ni.isValid() && !ni.atEnd());
                            shouldEqual(vi.point()+neighborOffsets[k], *ni);
                            ++ni;
                        }
                    }
                    should(ni == nend);
                    should(ni.atEnd() && !ni.isValid());
                }
                
                {
                    GridGraphNeighborIterator<N, true> ni(relativeOffsets[borderType], 
                                                          neighborIndices[borderType], 
                                                          backIndices[borderType], 
                                                          vi.point()),
                                                       nend = ni.getEndIterator();
                    
                    for(int k=0; k<neighborExists[borderType].size()/2; ++k)
                    {
                        if(neighborExists[borderType][k])
                        {
                            should(ni.isValid() && !ni.atEnd());
                            shouldEqual(vi.point()+neighborOffsets[k], *ni);
                            ++ni;
                        }
                    }
                    should(ni == nend);
                    should(ni.atEnd() && !ni.isValid());
                }
            }
        }
    }
    
    template <NeighborhoodType NType>
    void testOutArcIteratorDirected()
    {
        detail::makeArrayNeighborhood(neighborOffsets, neighborExists, NType);
        detail::computeNeighborOffsets(neighborOffsets, neighborExists, relativeOffsets, edgeDescrOffsets, neighborIndices, backIndices, true);

        // check neighborhoods at ROI border
        CoordinateIterator<N> i(Shape(3)), iend = i.getEndIterator();
        ArrayND<N, int> a(Shape(3));
        typedef typename ArrayND<N, int>::view_type View;
        
        for(; i != iend; ++i)
        {
            // create all possible array shapes from 1**N to 3**N
            View va = a.subarray(Shape(), *i+Shape(1)); 
            
            // check neighborhood of all pixels
            typename View::iterator vi = va.begin(), viend = vi.getEndIterator();
            for(; vi != viend; ++vi)
            {
                int borderType = vi.borderType();
                
                {
                    GridGraphOutArcIterator<N, false> ni(edgeDescrOffsets[borderType], 
                                                         neighborIndices[borderType], 
                                                         backIndices[borderType], 
                                                         vi.point()),
                                                      nend = ni.getEndIterator();
                    
                    for(int k=0; k<neighborExists[borderType].size(); ++k)
                    {
                        if(neighborExists[borderType][k])
                        {
                            should(ni.isValid() && !ni.atEnd());
                            shouldEqual(vi.point(), ni->vertexDescriptor());
                            shouldEqual(k, ni->edgeIndex());
                            shouldEqual(k, ni.neighborIndex());
                            should(!ni->isReversed());
                            ++ni;
                        }
                    }
                    should(ni == nend);
                    should(ni.atEnd() && !ni.isValid());
                }
                        
                {
                    GridGraphOutArcIterator<N, true> ni(edgeDescrOffsets[borderType], 
                                                        neighborIndices[borderType], 
                                                        backIndices[borderType], 
                                                        vi.point()),
                                                     nend = ni.getEndIterator();
                    
                    for(int k=0; k<neighborExists[borderType].size()/2; ++k)
                    {
                        if(neighborExists[borderType][k])
                        {
                            should(ni.isValid() && !ni.atEnd());
                            shouldEqual(vi.point(), ni->vertexDescriptor());
                            shouldEqual(k, ni->edgeIndex());
                            shouldEqual(k, ni.neighborIndex());
                            should(!ni->isReversed());
                            ++ni;
                        }
                    }
                    should(ni == nend);
                    should(ni.atEnd() && !ni.isValid());
                }
            }
        }
    }
    
    template <NeighborhoodType NType>
    void testOutArcIteratorUndirected()
    {
        detail::makeArrayNeighborhood(neighborOffsets, neighborExists, NType);
        detail::computeNeighborOffsets(neighborOffsets, neighborExists, relativeOffsets, edgeDescrOffsets, neighborIndices, backIndices, false);

        // check neighborhoods at ROI border
        CoordinateIterator<N> i(Shape(3)), iend = i.getEndIterator();
        ArrayND<N, int> a(Shape(3));
        typedef typename ArrayND<N, int>::view_type View;
        
        for(; i != iend; ++i)
        {
            // create all possible array shapes from 1**N to 3**N
            View va = a.subarray(Shape(), *i+Shape(1)); 
            
            // check neighborhood of all pixels
            typename View::iterator vi = va.begin(), viend = vi.getEndIterator();
            for(; vi != viend; ++vi)
            {
                int borderType = vi.borderType();
                
                {
                    GridGraphOutArcIterator<N> ni(edgeDescrOffsets[borderType], 
                                                  neighborIndices[borderType], 
                                                  backIndices[borderType], 
                                                  vi.point()),
                                                nend = ni.getEndIterator();
                    
                    for(int k=0; k<neighborExists[borderType].size(); ++k)
                    {
                        if(neighborExists[borderType][k])
                        {
                            should(ni.isValid() && !ni.atEnd());
                            shouldEqual(k, ni.neighborIndex());
                            if(k < neighborExists[borderType].size() / 2)
                            {
                                shouldEqual(vi.point(), ni->vertexDescriptor());
                                shouldEqual(k, ni->edgeIndex());
                                should(!ni->isReversed());
                            }
                            else
                            {
                                shouldEqual(vi.point()+neighborOffsets[k], ni->vertexDescriptor());
                                shouldEqual(k, (int)neighborOffsets.size() - ni->edgeIndex() - 1);
                                should(ni->isReversed());
                            }
                            ++ni;
                        }
                    }
                    should(ni == nend);
                    should(ni.atEnd() && !ni.isValid());
                }
                        
                {
                    GridGraphOutArcIterator<N, true> ni(edgeDescrOffsets[borderType], 
                                                        neighborIndices[borderType], 
                                                        backIndices[borderType], 
                                                        vi.point()),
                                                     nend = ni.getEndIterator();
                    
                    for(int k=0; k<neighborExists[borderType].size()/2; ++k)
                    {
                        if(neighborExists[borderType][k])
                        {
                            should(ni.isValid() && !ni.atEnd());
                            shouldEqual(k, ni.neighborIndex());
                            shouldEqual(vi.point(), ni->vertexDescriptor());
                            shouldEqual(k, ni->edgeIndex());
                            should(!ni->isReversed());
                            ++ni;
                        }
                    }
                    should(ni == nend);
                    should(ni.atEnd() && !ni.isValid());
                }
            }
        }
    }
    
    template <NeighborhoodType NType>
    void testArcIteratorDirected()
    {
        detail::makeArrayNeighborhood(neighborOffsets, neighborExists, NType);
        detail::computeNeighborOffsets(neighborOffsets, neighborExists, relativeOffsets, edgeDescrOffsets, neighborIndices, backIndices, true);

        // check neighborhoods at ROI border
        CoordinateIterator<N> i(Shape(3)), iend = i.getEndIterator();
        typedef typename MultiArrayShape<N+1>::type EdgeMapShape;
        
        for(; i != iend; ++i)
        {
            // create all possible array shapes from 1**N to 3**N
            Shape s = *i + Shape(1);
            
            EdgeMapShape es(gridGraphMaxDegree(N, NType));
            es.template subarray<0,N>() = s;
            ArrayND<N+1, int> edge_map(es);
            
            GridGraphArcIterator<N, false> ni(edgeDescrOffsets, neighborIndices, backIndices, s),
                                           nend = ni.getEndIterator();
            
            for(; ni != nend; ++ni)
            {
                should(ni.isValid() && !ni.atEnd());
                edge_map[*ni] += 1;
            }
            
            should(!ni.isValid() && ni.atEnd());
                
            // check neighborhood of all pixels
            CoordinateIterator<N> vi(s), viend = vi.getEndIterator();
            for(; vi != viend; ++vi)
            {
                int borderType = vi.borderType();
                es.template subarray<0,N>() = *vi;
                
                for(es[N]=0; es[N]<(MultiArrayIndex)neighborExists[borderType].size(); ++es[N])
                {
                    if(neighborExists[borderType][es[N]])
                        shouldEqual(edge_map[es], 1);
                    else
                        shouldEqual(edge_map[es], 0);
                }
            }
            
            shouldEqual(edge_map.template sum<int>(), gridGraphEdgeCount(s, NType, true));
        }
    }
    
    template <NeighborhoodType NType>
    void testArcIteratorUndirected()
    {
        detail::makeArrayNeighborhood(neighborOffsets, neighborExists, NType);
        detail::computeNeighborOffsets(neighborOffsets, neighborExists, relativeOffsets, edgeDescrOffsets, neighborIndices, backIndices, false);

        // check neighborhoods at ROI border
        CoordinateIterator<N> i(Shape(3)), iend = i.getEndIterator();
        typedef typename MultiArrayShape<N+1>::type EdgeMapShape;
        
        for(; i != iend; ++i)
        {
            // create all possible array shapes from 1**N to 3**N
            Shape s = *i + Shape(1);
            
            EdgeMapShape es(gridGraphMaxDegree(N, NType) / 2);
            es.template subarray<0,N>() = s;
            ArrayND<N+1, int> edge_map(es);
            
            GridGraphArcIterator<N, true> ni(edgeDescrOffsets, neighborIndices, backIndices, s),
                                          nend = ni.getEndIterator();
            
            for(; ni != nend; ++ni)
            {
                should(ni.isValid() && !ni.atEnd());
                edge_map[*ni] += 1;
            }
            
            should(!ni.isValid() && ni.atEnd());
                
            // check neighborhood of all pixels
            CoordinateIterator<N> vi(s), viend = vi.getEndIterator();
            for(; vi != viend; ++vi)
            {
                int borderType = vi.borderType();
                es.template subarray<0,N>() = *vi;
                
                for(es[N]=0; es[N]<(MultiArrayIndex)neighborExists[borderType].size()/2; ++es[N])
                {
                    if(neighborExists[borderType][es[N]])
                        shouldEqual(edge_map[es], 1);
                    else
                        shouldEqual(edge_map[es], 0);
                }
            }
            
            shouldEqual(edge_map.template sum<int>(), gridGraphEdgeCount(s, NType, false));
        }
    }
#endif
};

#if 0
template <int N>
struct GridGraphTests
{
    typedef typename MultiArrayShape<N>::type Shape;
    
    template <class DirectedTag, NeighborhoodType NType>
    void testBasics()
    {
        using namespace boost_graph;
        typedef GridGraph<N, DirectedTag> G;
        
        static const bool directed = IsSameType<DirectedTag, directed_tag>::value;
        
        CoordinateIterator<N> i(Shape(3)), iend = i.getEndIterator();        
        for(; i != iend; ++i)
        {
            // create all possible array shapes from 1**N to 3**N
            Shape s = *i + Shape(1);
            G g(s, NType);
            
#ifdef WITH_LEMON
            shouldEqual(directed, !lemon::UndirectedTagIndicator<G>::value);
#endif
            
            should(typename G::vertex_descriptor(lemon::INVALID) == lemon::INVALID);
            should(typename G::edge_descriptor(lemon::INVALID) == lemon::INVALID);
        
            shouldEqual(g.shape(), s);
            shouldEqual(g.isDirected(), directed);
            shouldEqual(g.num_vertices(), prod(s));
            shouldEqual(num_vertices(g), prod(s));
            shouldEqual(g.num_edges(), gridGraphEdgeCount(s, NType, directed));
            shouldEqual(num_edges(g), gridGraphEdgeCount(s, NType, directed));
            shouldEqual(g.maxDegree(), gridGraphMaxDegree(N, NType));
            
            shouldEqual(g.num_vertices(), g.nodeNum());
            shouldEqual(g.num_edges(), g.edgeNum());
            shouldEqual(g.arcNum(), directed ? g.edgeNum() : 2*g.edgeNum());
        }
#ifdef WITH_BOOST_GRAPH
        BOOST_CONCEPT_ASSERT(( boost::GraphConcept<G> ));
        BOOST_CONCEPT_ASSERT(( boost::IncidenceGraphConcept<G> ));
        BOOST_CONCEPT_ASSERT(( boost::BidirectionalGraphConcept<G> ));
        BOOST_CONCEPT_ASSERT(( boost::AdjacencyGraphConcept<G> ));
        BOOST_CONCEPT_ASSERT(( boost::VertexListGraphConcept<G> ));
        BOOST_CONCEPT_ASSERT(( boost::EdgeListGraphConcept<G> ));
        BOOST_CONCEPT_ASSERT(( boost::AdjacencyMatrixConcept<G> ));
#endif
    }
    
    template <class DirectedTag, NeighborhoodType NType>
    void testVertexIterator()
    {
        using namespace boost_graph;
        
        typedef GridGraph<N, DirectedTag> Graph;

        CoordinateIterator<N> i(Shape(3)), iend = i.getEndIterator();        
        for(; i != iend; ++i)
        {
            // create all possible array shapes from 1**N to 3**N
            Shape s = *i + Shape(1);
            Graph g(s, NType);
            typename Graph::template NodeMap<int> vertexMap(g);
            lemon::InDegMap<Graph>  inDegreeMap(g);
            lemon::OutDegMap<Graph> outDegreeMap(g);
            typename Graph::IndexMap indexMap(g.indexMap());
        
            int count = 0;
        
            typename Graph::vertex_iterator j = g.get_vertex_iterator(), 
                                            end = g.get_vertex_end_iterator();
            typename Graph::NodeIt lj(g);
                                            
            should(j == vertices(g).first);
            should(end == vertices(g).second);
            should(j == typename Graph::vertex_iterator(g));
            for(; j != end; ++j, ++lj, ++count)
            {
                should(j.isValid() && !j.atEnd());
                should(j == lj);
                should(j != lemon::INVALID);
                should(!(j == lemon::INVALID));
                
                shouldEqual(j.scanOrderIndex(), g.id(j));
                shouldEqual(j.scanOrderIndex(), g.id(*j));
                shouldEqual(*j, g.nodeFromId(g.id(*j)));
                
                shouldEqual(g.out_degree(j), g.out_degree(*j));
                shouldEqual(g.out_degree(j), out_degree(*j, g));
                shouldEqual(g.forward_degree(j), g.forward_degree(*j));
                shouldEqual(g.back_degree(j), g.back_degree(*j));
                shouldEqual(g.forward_degree(j) + g.back_degree(j), g.out_degree(j));
                shouldEqual(g.in_degree(j), g.out_degree(j));
                shouldEqual(g.degree(j), g.isDirected() ? 2*g.out_degree(j) : g.out_degree(j));
                shouldEqual(g.out_degree(j), boost_graph::out_degree(*j, g));
                shouldEqual(g.in_degree(j), boost_graph::in_degree(*j, g));
                shouldEqual(g.out_degree(j), outDegreeMap[*j]);
                shouldEqual(g.in_degree(j), inDegreeMap[*j]);

                shouldEqual(*j, indexMap[*j]);

                put(vertexMap, *j, get(vertexMap, *j) + 1); // same as: vertexMap[*j] += 1;
            }
            should(!j.isValid() && j.atEnd());
            should(j == lj);
            should(j == lemon::INVALID);
            should(!(j != lemon::INVALID));
            
            // check that all vertices are found exactly once
            shouldEqual(count, g.num_vertices());
            int min = NumericTraits<int>::max(), max = NumericTraits<int>::min();
            vertexMap.minmax(&min, &max);
            
            shouldEqual(min, 1);
            shouldEqual(max, 1);
        }
    }
    
    template <class DirectedTag, NeighborhoodType NType>
    void testNeighborIterator()
    {
        using namespace boost_graph;
        
        static const bool directed = IsSameType<DirectedTag, directed_tag>::value;
        
        typedef GridGraph<N, DirectedTag> Graph;
        typedef typename Graph::Node Node;
        
        CoordinateIterator<N> i(Shape(3)), iend = i.getEndIterator();        
        for(; i != iend; ++i)
        {
            // create all possible array shapes from 1**N to 3**N
            Shape s = *i + Shape(1);
            Graph g(s, NType);
            
            ArrayND<N, int> vertexMap(s);
            typename Graph::template EdgeMap<int> edgeMap(g);
            
            typename Graph::template ArcMap<int> arcIdMap(g);            
            typename Graph::template EdgeMap<int> edgeIdMap(g);            
            std::vector<unsigned char> arcExistsMap(g.maxArcId()+1, 0);
            std::vector<unsigned char> edgeExistsMap(g.maxEdgeId()+1, 0);

            linearSequence(arcIdMap.begin(), arcIdMap.end());
            linearSequence(edgeIdMap.begin(), edgeIdMap.end());
            
            shouldEqual((edgeMap.shape().template subarray<0, N>()), s);
            shouldEqual(edgeMap.shape(N), g.maxUniqueDegree());
            shouldEqual((arcIdMap.shape().template subarray<0, N>()), s);
            shouldEqual(arcIdMap.shape(N), g.maxDegree());
            shouldEqual(edgeMap.shape(), edgeIdMap.shape());

            int totalCount = 0;
        
            typename Graph::vertex_iterator j = g.get_vertex_iterator(), 
                                            end = g.get_vertex_end_iterator();
            for(; j != end; ++j)
            {
                typename Graph::neighbor_vertex_iterator n = g.get_neighbor_vertex_iterator(j), 
                                                         nend = g.get_neighbor_vertex_end_iterator(j);
                typename Graph::out_edge_iterator        e = g.get_out_edge_iterator(j), 
                                                         eend = g.get_out_edge_end_iterator(j);
                typename Graph::in_edge_iterator         i = g.get_in_edge_iterator(j), 
                                                         iend = g.get_in_edge_end_iterator(j);
                typename Graph::IncEdgeIt                le(g, j);
                typename Graph::OutArcIt                 la(g, j);
                typename Graph::InArcIt                  li(g, j);

                should(n == g.get_neighbor_vertex_iterator(*j));
                should(nend == g.get_neighbor_vertex_end_iterator(*j));
                should(n == adjacent_vertices(*j, g).first);
                should(nend == adjacent_vertices(*j, g).second);
                should(n == adjacent_vertices_at_iterator(j, g).first);
                should(nend == adjacent_vertices_at_iterator(j, g).second);
                should(e == g.get_out_edge_iterator(*j));
                should(eend == g.get_out_edge_end_iterator(*j));
                should(e == out_edges(*j, g).first);
                should(eend == out_edges(*j, g).second);
                should(i == in_edges(*j, g).first);
                should(iend == in_edges(*j, g).second);

                int count = 0;
                for(; n != nend; ++n, ++e, ++i, ++la, ++le, ++li, ++count)
                {
                    should(*n != *j);
                    should(n.isValid() && !n.atEnd());
                    should(n != lemon::INVALID);
                    should(!(n == lemon::INVALID));
                    should(e.isValid() && !e.atEnd());
                    should(e == la);
                    should(e == le);
                    should(e != eend);
                    should(e != lemon::INVALID);
                    should(!(e == lemon::INVALID));
                    should(i != lemon::INVALID);
                    should(!(i == lemon::INVALID));
                    should(i == li);
                    
                    shouldEqual(source(*e, g), *j);
                    shouldEqual(target(*e, g), *n);
                    vertexMap[*n] += 1;
                    edgeMap[*e] += 1;
                    
                    shouldEqual(source(*i, g), *n);
                    shouldEqual(target(*i, g), *j);
                    
                    typename Graph::Arc a  = g.findArc(*j, *n),
                                        oa = g.findArc(*n, *j);
                    should(a == *la);
                    shouldEqual(source(oa, g), *n);
                    shouldEqual(target(oa, g), *j);
                    shouldEqual(g.baseNode(la), *j);
                    shouldEqual(g.runningNode(la), *n);
                    shouldEqual(g.oppositeArc(a), oa);
                    
                    typename Graph::Edge ge = g.findEdge(*j, *n),
                                         oe = g.findEdge(*n, *j);
                    should(ge == *le);
                    shouldEqual(g.baseNode(le), *j);
                    shouldEqual(g.runningNode(le), *n);
                    
                    typename Graph::Node u = g.u(oe), 
                                         v = g.v(oe);
                    if(directed)
                    {
                        should(g.direction(*la));
                        shouldEqual(g.direct(*le, true), *la);
                        shouldEqual(g.direct(*le, false), g.oppositeArc(*la));
                    }
                    else
                    {
                        should(oe == *le);
                        if(g.id(n) < g.id(j))
                        {
                            std::swap(u, v);
                            should(g.direction(*la));
                            shouldEqual(g.direct(*le, true), *la);
                            shouldEqual(g.direct(*le, false), g.oppositeArc(*la));
                        }
                        else
                        {
                            should(!g.direction(*la));
                            shouldEqual(g.direct(*le, false), *la);
                            shouldEqual(g.direct(*le, true), g.oppositeArc(*la));
                        }
                    }

                    shouldEqual(u, *n);
                    shouldEqual(v, *j);
                    shouldEqual(g.direct(*le, v), *la);
                    shouldEqual(g.direct(*le, u), g.oppositeArc(*la));
                    shouldEqual(g.oppositeNode(u, *le), v);
                    shouldEqual(g.oppositeNode(v, *le), u);
                    shouldEqual(g.oppositeNode(lemon::INVALID, *le), Node(lemon::INVALID));
                    
                    MultiArrayIndex arcId = g.id(*la);
                    shouldEqual(arcIdMap[*la], arcId);
                    arcExistsMap[arcId] = 1;  // mark arc as found

                    MultiArrayIndex edgeId = g.id(*le);
                    shouldEqual(edgeIdMap[*la], g.id((typename Graph::Edge &)*la));
                    shouldEqual(edgeIdMap[*le], g.id(*le));
                    edgeExistsMap[edgeId] = 1;  // mark edge as found

                    shouldEqual(*la, g.arcFromId(g.id(*la)));
                    shouldEqual(*le, g.edgeFromId(g.id(*le)));
                }
                should(!n.isValid() && n.atEnd());
                should(n == lemon::INVALID);
                should(!(n != lemon::INVALID));
                should(!e.isValid() && e.atEnd());
                should(e == eend);
                should(e == la);
                should(e == le);
                should(e == lemon::INVALID);
                should(!(e != lemon::INVALID));
                should(i == li);
                should(i == iend);
                should(i == lemon::INVALID);
                should(!(i != lemon::INVALID));
                
                shouldEqual(count, g.out_degree(j));
                
                totalCount += count;
            }
            
            // check that all neighbors are found
            if(!directed)
                totalCount /= 2;
            shouldEqual(totalCount, g.num_edges());
            
            int min = NumericTraits<int>::max(), max = NumericTraits<int>::min();
            edgeMap.minmax(&min, &max);
            shouldEqual(min, 0);
            shouldEqual(max, g.num_edges() ? !directed ? 2 : 1 : 0);

            j = g.get_vertex_iterator();
            for(; j != end; ++j)
            {
                shouldEqual(vertexMap[*j], g.in_degree(j));
                if(directed)
                    shouldEqual(edgeMap.bindInner(*j).template sum<int>(), g.out_degree(j));
                else
                    shouldEqual(edgeMap.bindInner(*j).template sum<int>(), 2*g.back_degree(j));
            }

            for(int id=0; id <= g.maxEdgeId(); ++id)
            {
                typename Graph::Edge e = g.edgeFromId(id);
                if(edgeExistsMap[id])
                    should(e != lemon::INVALID);
                else
                    should(e == lemon::INVALID);
            }

            for(int id=0; id <= g.maxArcId(); ++id)
            {
                typename Graph::Arc a = g.arcFromId(id);
                if(arcExistsMap[id])
                    should(a != lemon::INVALID);
                else
                    should(a == lemon::INVALID);
            }
        }
    }
    
    template <class DirectedTag, NeighborhoodType NType>
    void testBackNeighborIterator()
    {
        using namespace boost_graph;
        
        static const bool directed = IsSameType<DirectedTag, directed_tag>::value;
        
        typedef GridGraph<N, DirectedTag> Graph;
        
        CoordinateIterator<N> i(Shape(3)), iend = i.getEndIterator();        
        for(; i != iend; ++i)
        {
            // create all possible array shapes from 1**N to 3**N
            Shape s = *i + Shape(1);
            Graph g(s, NType);
            
            ArrayND<N, int> vertexMap(s);
            typename Graph::template EdgeMap<int> edgeMap(g);
            typename Graph::template ArcMap<int> arcMap(g);
            
            typename Graph::template ArcMap<int> arcIdMap(g);            
            typename Graph::template EdgeMap<int> edgeIdMap(g);            

            linearSequence(arcIdMap.begin(), arcIdMap.end());
            linearSequence(edgeIdMap.begin(), edgeIdMap.end());
            
            shouldEqual((edgeMap.shape().template subarray<0, N>()), s);
            shouldEqual(edgeMap.shape(N), g.maxUniqueDegree());
            shouldEqual((arcIdMap.shape().template subarray<0, N>()), s);
            shouldEqual(arcIdMap.shape(N), g.maxDegree());
            shouldEqual(edgeMap.shape(), edgeIdMap.shape());
            
            int totalCount = 0;
        
            typename Graph::vertex_iterator j = g.get_vertex_iterator(), 
                                            end = g.get_vertex_end_iterator();
            for(; j != end; ++j)
            {
                typename Graph::back_neighbor_vertex_iterator n = g.get_back_neighbor_vertex_iterator(j), 
                                                              nend = g.get_back_neighbor_vertex_end_iterator(j);
                typename Graph::out_back_edge_iterator        e = g.get_out_back_edge_iterator(j), 
                                                              eend = g.get_out_back_edge_end_iterator(j);
                typename Graph::IncBackEdgeIt                 le(g, j);
                typename Graph::OutBackArcIt                  la(g, j);

                should(n == g.get_back_neighbor_vertex_iterator(*j));
                should(nend == g.get_back_neighbor_vertex_end_iterator(*j));
                should(n == back_adjacent_vertices(*j, g).first);
                should(nend == back_adjacent_vertices(*j, g).second);
                should(e == g.get_out_back_edge_iterator(*j));
                should(eend == g.get_out_back_edge_end_iterator(*j));
                should(e == out_back_edges(*j, g).first);
                should(eend == out_back_edges(*j, g).second);

                int count = 0;
                for(; n != nend; ++n, ++e, ++la, ++le, ++count)
                {
                    should(*n != *j);
                    should(n.isValid() && !n.atEnd());
                    should(n != lemon::INVALID);
                    should(!(n == lemon::INVALID));
                    should(e.isValid() && !e.atEnd());
                    should(e == la);
                    should(e == le);
                    should(e != eend);
                    should(e != lemon::INVALID);
                    should(!(e == lemon::INVALID));
                    
                    shouldEqual(source(*e, g), *j);
                    shouldEqual(target(*e, g), *n);
                    vertexMap[*n] += 1;
                    edgeMap[*e] += 1;
                    arcMap[*la] += 1;
                    
                    typename Graph::Arc a  = g.findArc(*j, *n),
                                        oa = g.findArc(*n, *j);
                    should(a == *la);
                    shouldEqual(source(oa, g), *n);
                    shouldEqual(target(oa, g), *j);
                    shouldEqual(g.baseNode(la), *j);
                    shouldEqual(g.runningNode(la), *n);
                    shouldEqual(g.oppositeArc(a), oa);
                    
                    typename Graph::Edge ge = g.findEdge(*j, *n),
                                         oe = g.findEdge(*n, *j);
                    should(ge == *le);
                    shouldEqual(g.baseNode(le), *j);
                    shouldEqual(g.runningNode(le), *n);
                    
                    typename Graph::Node u = g.u(oe), 
                                         v = g.v(oe);
                    if(!directed)
                    {
                        should(oe == *le);
                        if(g.id(n) < g.id(j))
                            std::swap(u, v);
                    }
                    shouldEqual(u, *n);
                    shouldEqual(v, *j);
                    
                    shouldEqual(arcIdMap[*la], g.id(la));
                    shouldEqual(edgeIdMap[*la], g.id((typename Graph::Edge &)*la));
                    shouldEqual(edgeIdMap[*le], g.id(*le));
                    shouldEqual(*la, g.arcFromId(g.id(*la)));
                    shouldEqual(*le, g.edgeFromId(g.id(*le)));
                }
                should(!n.isValid() && n.atEnd());
                should(n == lemon::INVALID);
                should(!(n != lemon::INVALID));
                should(!e.isValid() && e.atEnd());
                should(e == eend);
                should(e == la);
                should(e == le);
                should(e == lemon::INVALID);
                should(!(e != lemon::INVALID));
                
                shouldEqual(count, g.back_degree(j));
                
                totalCount += count;
            }
            
            // check that all neighbors are found
            if(directed)
                totalCount *= 2;
            shouldEqual(totalCount, g.num_edges());
            
            int min = NumericTraits<int>::max(), max = NumericTraits<int>::min();
            edgeMap.minmax(&min, &max);
            shouldEqual(min, 0);
            shouldEqual(max, g.num_edges() ? 1 : 0);

            typename Graph::edge_propmap_shape_type estart, estop;
            estop.template subarray<0,N>() = s;
            estop[N] = g.maxDegree() / 2;
            shouldEqualSequence(edgeMap.begin(), edgeMap.end(), arcMap.subarray(estart, estop).begin());
            
            estart[N] = g.maxDegree() / 2;
            estop[N] = g.maxDegree();
            min = NumericTraits<int>::max(), max = NumericTraits<int>::min();
            arcMap.subarray(estart, estop).minmax(&min, &max);
            shouldEqual(min, 0);
            shouldEqual(max, 0);

            j = g.get_vertex_iterator();
            for(; j != end; ++j)
            {
                shouldEqual(vertexMap[*j], g.forward_degree(j));
                shouldEqual(edgeMap.bindInner(*j).template sum<int>(), g.back_degree(j));
            }
        }
    }
    
    template <class DirectedTag, NeighborhoodType NType>
    void testEdgeIterator()
    {
        using namespace boost_graph;
        
        static const bool directed = IsSameType<DirectedTag, directed_tag>::value;
        
        typedef GridGraph<N, DirectedTag> Graph;
        
        CoordinateIterator<N> i(Shape(3)), iend = i.getEndIterator();        
        for(; i != iend; ++i)
        {
            // create all possible array shapes from 1**N to 3**N
            Shape s = *i + Shape(1);
            Graph g(s, NType);
            
            ArrayND<N, int> sourceVertexMap(s),targetVertexMap(s);
            ArrayND<N+1, int> edgeMap(g.edge_propmap_shape()),
                                 edgeIdMap(g.edge_propmap_shape());
            
            shouldEqual((edgeMap.shape().template subarray<0, N>()), s);
            shouldEqual(edgeMap.shape(N), g.maxUniqueDegree());
            
            linearSequence(edgeIdMap.begin(), edgeIdMap.end());
            
            typename Graph::edge_iterator e = g.get_edge_iterator(),
                                          eend = g.get_edge_end_iterator();
            typename Graph::EdgeIt el(g);

            should(e == edges(g).first);
            should(eend == edges(g).second);
            
            int count = 0;
            int maxEdgeId = -1;
            for(; e != eend; ++e, ++el, ++count)
            {
                should(e.isValid() && !e.atEnd());
                should(e == el);
                should(e != lemon::INVALID);
                should(!(e == lemon::INVALID));

                put(edgeMap, *e, get(edgeMap, *e) + 1); // same as: edgeMap[*e] += 1;
                sourceVertexMap[source(*e, g)] += 1;
                targetVertexMap[target(*e, g)] += 1;
                
                shouldEqual(edgeIdMap[*e], g.id(el));
                shouldEqual(*el, g.edgeFromId(g.id(*el)));

                if(maxEdgeId < g.id(el))
                    maxEdgeId = g.id(el);
            }
            should(!e.isValid() && e.atEnd());
            should(e == el);
            should(e == lemon::INVALID);
            should(!(e != lemon::INVALID));
            
            shouldEqual(maxEdgeId, g.maxEdgeId());
            
            // check that all neighbors are found
            shouldEqual(count, g.num_edges());
            
            int min = NumericTraits<int>::max(), max = NumericTraits<int>::min();
            edgeMap.minmax(&min, &max);
            shouldEqual(min, 0);
            shouldEqual(max, g.num_edges() ? 1 : 0);
            
            CoordinateIterator<N> j(s), end = j.getEndIterator();
            for(; j != end; ++j)
            {
                if(directed)
                {
                    shouldEqual(edgeMap.bindInner(*j).template sum<int>(), g.out_degree(j));
                    shouldEqual(sourceVertexMap[*j], g.out_degree(j));
                    shouldEqual(targetVertexMap[*j], g.out_degree(j));
                }
                else
                {
                    shouldEqual(edgeMap.bindInner(*j).template sum<int>(), g.back_degree(j));
                    shouldEqual(sourceVertexMap[*j], g.back_degree(j));
                    shouldEqual(targetVertexMap[*j], g.forward_degree(j));
                }
            }
        }
    }
    
    template <class DirectedTag, NeighborhoodType NType>
    void testArcIterator()
    {
        using namespace boost_graph;
        
        static const bool directed = IsSameType<DirectedTag, directed_tag>::value;
        
        typedef GridGraph<N, DirectedTag> Graph;
        
        CoordinateIterator<N> i(Shape(3)), iend = i.getEndIterator();        
        for(; i != iend; ++i)
        {
            // create all possible array shapes from 1**N to 3**N
            Shape s = *i + Shape(1);
            Graph g(s, NType);
            
            typename Graph::template NodeMap<int> sourceVertexMap(g), targetVertexMap(g);
            typename Graph::template EdgeMap<int> edgeMap(g);
            typename Graph::template ArcMap<int> arcMap(g);
            
            shouldEqual(sourceVertexMap.shape(), s);
            shouldEqual(targetVertexMap.shape(), s);
            shouldEqual((edgeMap.shape().template subarray<0, N>()), s);
            shouldEqual(edgeMap.shape(N), g.maxUniqueDegree());
            shouldEqual((arcMap.shape().template subarray<0, N>()), s);
            shouldEqual(arcMap.shape(N), g.maxDegree());
            
            typename Graph::ArcIt e(g);

            int count = 0;
            int maxArcId = -1;
            for(; e != lemon::INVALID; ++e, ++count)
            {
                should(e.isValid() && !e.atEnd());
                should(e != lemon::INVALID);
                should(!(e == lemon::INVALID));

                sourceVertexMap[source(*e, g)] += 1;
                targetVertexMap[target(*e, g)] += 1;
                edgeMap[*e] += 1;
                arcMap[*e] += 1;
                
                shouldEqual(*e, g.arcFromId(g.id(*e)));
                
                if(maxArcId < g.id(e))
                    maxArcId = g.id(e);
            }
            should(!e.isValid() && e.atEnd());
            should(e == lemon::INVALID);
            should(!(e != lemon::INVALID));
            
            shouldEqual(maxArcId, g.maxArcId());
            
            // check that all neighbors are found
            shouldEqual(count, g.arcNum());
            shouldEqual(count, directed ? g.edgeNum() : 2*g.edgeNum());
            
            int min = NumericTraits<int>::max(), max = NumericTraits<int>::min();
            edgeMap.minmax(&min, &max);
            shouldEqual(min, 0);
            shouldEqual(max, g.arcNum() ? directed ? 1 : 2 : 0);
            
            min = NumericTraits<int>::max();
            max = NumericTraits<int>::min();
            arcMap.minmax(&min, &max);
            shouldEqual(min, 0);
            shouldEqual(max, g.arcNum() ? 1 : 0);
            
            CoordinateIterator<N> j(s), end = j.getEndIterator();
            for(; j != end; ++j)
            {
                shouldEqual(edgeMap.bindInner(*j).template sum<int>(), directed ? g.out_degree(j) : 2*g.back_degree(j));
                shouldEqual(arcMap.bindInner(*j).template sum<int>(), g.out_degree(j));
                shouldEqual(sourceVertexMap[*j], g.out_degree(j));
                shouldEqual(targetVertexMap[*j], g.out_degree(j));
            }
        }
    }
};

template <unsigned int N>
struct GridGraphAlgorithmTests
{
    typedef typename MultiArrayShape<N>::type Shape;
    
    template <class DirectedTag, NeighborhoodType NType>
    void testLocalMinMax()
    {
        typedef GridGraph<N, DirectedTag> Graph;
        
        Graph g(Shape(3), NType);
        typename Graph::template NodeMap<int> src(g), dest(g);
        
        src[Shape(1)] = 1;
        
        should(1 == boost_graph::localMinMaxGraph(g, src, dest, 1, -9999, std::greater<int>()));
        
        shouldEqualSequence(src.begin(), src.end(), dest.begin());
        
        dest.init(0);
        
        should(1 == lemon_graph::localMinMaxGraph(g, src, dest, 1, -9999, std::greater<int>()));
        
        shouldEqualSequence(src.begin(), src.end(), dest.begin());
    }
};
#endif

template <int N>
struct GridgraphTestSuiteN
: public vigra::test_suite
{
    GridgraphTestSuiteN()
    : vigra::test_suite((std::string("GridGraph<") + std::to_string(N) + ">").c_str())
    {
        add(testCase(&NeighborhoodTests<N>::testVertexIterator));

        add(testCase(&NeighborhoodTests<N>::testDirectNeighborhoodFOrder));
        add(testCase(&NeighborhoodTests<N>::testDirectNeighborhoodCOrder));
        add(testCase(&NeighborhoodTests<N>::testIndirectNeighborhoodFOrder));
        add(testCase(&NeighborhoodTests<N>::testIndirectNeighborhoodCOrder));
#if 0
        add(testCase(&NeighborhoodTests<N>::template testNeighborhoodIterator<DirectNeighborhood>));
        add(testCase(&NeighborhoodTests<N>::template testNeighborhoodIterator<IndirectNeighborhood>));
        
        add(testCase(&NeighborhoodTests<N>::template testOutArcIteratorDirected<DirectNeighborhood>));
        add(testCase(&NeighborhoodTests<N>::template testOutArcIteratorDirected<IndirectNeighborhood>));
        
        add(testCase(&NeighborhoodTests<N>::template testOutArcIteratorUndirected<DirectNeighborhood>));
        add(testCase(&NeighborhoodTests<N>::template testOutArcIteratorUndirected<IndirectNeighborhood>));
        
        add(testCase(&NeighborhoodTests<N>::template testArcIteratorDirected<DirectNeighborhood>));
        add(testCase(&NeighborhoodTests<N>::template testArcIteratorDirected<IndirectNeighborhood>));
        
        add(testCase(&NeighborhoodTests<N>::template testArcIteratorUndirected<DirectNeighborhood>));
        add(testCase(&NeighborhoodTests<N>::template testArcIteratorUndirected<IndirectNeighborhood>));
        
        add(testCase((&GridGraphTests<N>::template testBasics<directed_tag, IndirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testBasics<undirected_tag, IndirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testBasics<directed_tag, DirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testBasics<undirected_tag, DirectNeighborhood>)));
        
        add(testCase((&GridGraphTests<N>::template testVertexIterator<directed_tag, IndirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testVertexIterator<undirected_tag, IndirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testVertexIterator<directed_tag, DirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testVertexIterator<undirected_tag, DirectNeighborhood>)));
        
        add(testCase((&GridGraphTests<N>::template testNeighborIterator<directed_tag, IndirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testNeighborIterator<undirected_tag, IndirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testNeighborIterator<directed_tag, DirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testNeighborIterator<undirected_tag, DirectNeighborhood>)));
        
        add(testCase((&GridGraphTests<N>::template testBackNeighborIterator<directed_tag, IndirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testBackNeighborIterator<undirected_tag, IndirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testBackNeighborIterator<directed_tag, DirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testBackNeighborIterator<undirected_tag, DirectNeighborhood>)));
        
        add(testCase((&GridGraphTests<N>::template testEdgeIterator<directed_tag, IndirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testEdgeIterator<undirected_tag, IndirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testEdgeIterator<directed_tag, DirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testEdgeIterator<undirected_tag, DirectNeighborhood>)));
        
        add(testCase((&GridGraphTests<N>::template testArcIterator<directed_tag, IndirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testArcIterator<undirected_tag, IndirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testArcIterator<directed_tag, DirectNeighborhood>)));
        add(testCase((&GridGraphTests<N>::template testArcIterator<undirected_tag, DirectNeighborhood>)));
        
        add(testCase((&GridGraphAlgorithmTests<N>::template testLocalMinMax<undirected_tag, DirectNeighborhood>)));
#endif
    }
};

struct GridgraphTestSuite
: public vigra::test_suite
{
    GridgraphTestSuite()
    : vigra::test_suite("GridgraphTestSuite")
    {
        add(VIGRA_TEST_SUITE(GridgraphTestSuiteN<2>));
        add(VIGRA_TEST_SUITE(GridgraphTestSuiteN<3>));
        add(VIGRA_TEST_SUITE(GridgraphTestSuiteN<runtime_size>));
        //        add(VIGRA_TEST_SUITE(GridgraphTestSuiteN<4>));
    }
};

int main(int argc, char **argv)
{

    GridgraphTestSuite gridgraphTest;

    int failed = gridgraphTest.run(vigra::testsToBeExecuted(argc, argv));

    std::cout << gridgraphTest.report() << std::endl;

    return (failed != 0);
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
