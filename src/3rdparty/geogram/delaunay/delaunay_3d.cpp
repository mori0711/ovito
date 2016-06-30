/*
 *  Copyright (c) 2012-2014, Bruno Levy
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *  * Neither the name of the ALICE Project-Team nor the names of its
 *  contributors may be used to endorse or promote products derived from this
 *  software without specific prior written permission.
 * 
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  If you modify this software, you should include a notice giving the
 *  name of the person performing the modification, the date of modification,
 *  and the reason for such modification.
 *
 *  Contact: Bruno Levy
 *
 *     Bruno.Levy@inria.fr
 *     http://www.loria.fr/~levy
 *
 *     ALICE Project
 *     LORIA, INRIA Lorraine, 
 *     Campus Scientifique, BP 239
 *     54506 VANDOEUVRE LES NANCY CEDEX 
 *     FRANCE
 *
 */

#include <geogram/delaunay/delaunay_3d.h>
#include <geogram/basic/geometry_nd.h>
#include <geogram/basic/stopwatch.h>
#include <geogram/basic/matrix.h>
#include <geogram/basic/permutation.h>
#include <geogram/basic/thread_sync.h>
#include <geogram/mesh/mesh_reorder.h>
#include <stack>

// TODO: optimizations:
// - convex hull traversal for nearest_vertex()

namespace {
    using namespace GEO;

    /**
     * \brief Tests whether two 3d points are identical.
     * \param[in] p1 first point
     * \param[in] p2 second point
     * \retval true if \p p1 and \p p2 have exactly the same
     *  coordinates
     * \retval false otherwise
     */
    bool points_are_identical(
        const double* p1,
        const double* p2
    ) {
        return
            (p1[0] == p2[0]) &&
            (p1[1] == p2[1]) &&
            (p1[2] == p2[2])
        ;
    }

    /**
     * \brief Tests whether three 3d points are colinear.
     * \param[in] p1 first point
     * \param[in] p2 second point
     * \param[in] p3 third point
     * \retval true if \p p1, \p p2 and \p p3 are colinear
     * \retbal false otherwise
     */
    bool points_are_colinear(
        const double* p1,
        const double* p2,
        const double* p3
    ) {
        // Colinearity is tested by using four coplanarity
        // tests with four points that are not coplanar.
        static const double q000[3] = {0.0, 0.0, 0.0};
        static const double q001[3] = {0.0, 0.0, 1.0};
        static const double q010[3] = {0.0, 1.0, 0.0};
        static const double q100[3] = {1.0, 0.0, 0.0};
        return
            PCK::orient_3d(p1, p2, p3, q000) == ZERO &&
            PCK::orient_3d(p1, p2, p3, q001) == ZERO &&
            PCK::orient_3d(p1, p2, p3, q010) == ZERO &&
            PCK::orient_3d(p1, p2, p3, q100) == ZERO
        ;
    }

    /**
     * \brief Computes the (approximate) orientation predicate in 3d.
     * \details Computes the sign of the (approximate) signed volume of
     *  the tetrahedron p0, p1, p2, p3.
     * \param[in] p0 first vertex of the tetrahedron
     * \param[in] p1 second vertex of the tetrahedron
     * \param[in] p2 third vertex of the tetrahedron
     * \param[in] p3 fourth vertex of the tetrahedron
     * \retval POSITIVE if the tetrahedron is oriented positively
     * \retval ZERO if the tetrahedron is flat
     * \retval NEGATIVE if the tetrahedron is oriented negatively
     * \todo check whether orientation is inverted as compared to 
     *   Shewchuk's version.
     */
    inline Sign orient_3d_inexact(
        const double* p0, const double* p1,
        const double* p2, const double* p3
    ) {
        double a11 = p1[0] - p0[0] ;
        double a12 = p1[1] - p0[1] ;
        double a13 = p1[2] - p0[2] ;
        
        double a21 = p2[0] - p0[0] ;
        double a22 = p2[1] - p0[1] ;
        double a23 = p2[2] - p0[2] ;
        
        double a31 = p3[0] - p0[0] ;
        double a32 = p3[1] - p0[1] ;
        double a33 = p3[2] - p0[2] ;

        double Delta = det3x3(
            a11,a12,a13,
            a21,a22,a23,
            a31,a32,a33
        );

        return geo_sgn(Delta);
    }
}

namespace GEO {

    char Delaunay3d::halfedge_facet_[4][4] = {
        {4, 2, 3, 1},
        {3, 4, 0, 2},
        {1, 3, 4, 0},
        {2, 0, 1, 4}
    };

    // tet facet vertex is such that the tetrahedron
    // formed with:
    //  vertex lv
    //  tet_facet_vertex[lv][0]
    //  tet_facet_vertex[lv][1]
    //  tet_facet_vertex[lv][2]
    // has the same orientation as the original tetrahedron for
    // any vertex lv.

    char Delaunay3d::tet_facet_vertex_[4][3] = {
        {1, 2, 3},
        {0, 3, 2},
        {3, 0, 1},
        {1, 0, 2}
    };

    Delaunay3d::Delaunay3d(coord_index_t dimension) :
        Delaunay(dimension)
    {
        if(dimension != 3 && dimension != 4) {
            throw InvalidDimension(dimension, "Delaunay3d", "3 or 4");
        }
        first_free_ = END_OF_LIST;
        weighted_ = (dimension == 4);
        // In weighted mode, vertices are 4d but combinatorics is 3d.
        if(weighted_) {
            cell_size_ = 4;
            cell_v_stride_ = 4;
            cell_neigh_stride_ = 4;
        }
        cur_stamp_ = 0;
        debug_mode_ = false;
        verbose_debug_mode_ = false;
        debug_mode_ = (debug_mode_ || verbose_debug_mode_);
        benchmark_mode_ = false;
    }

    Delaunay3d::~Delaunay3d() {
    }

    bool Delaunay3d::set_vertices(
        index_t nb_vertices, const double* vertices, const std::function<bool(int,int)>& progressCallback
    ) {
        Stopwatch* W = nil;
        if(benchmark_mode_) {
            W = new Stopwatch("DelInternal");
        }
        cur_stamp_ = 0;
        if(weighted_) {
            heights_.resize(nb_vertices);
            for(index_t i = 0; i < nb_vertices; ++i) {
                // Client code uses 4d embedding with ti = sqrt(W - wi)
                //   where W = max(wi)
                // We recompute the standard "shifted" lifting on
                // the paraboloid from it.
                // (we use wi - W, everything is shifted by W, but
                // we do not care since the power diagram is invariant
                // by a translation of all weights).
                double w = -geo_sqr(vertices[4 * i + 3]);
                heights_[i] = -w +
                    geo_sqr(vertices[4 * i]) +
                    geo_sqr(vertices[4 * i + 1]) +
                    geo_sqr(vertices[4 * i + 2]);
            }
        }

        Delaunay::set_vertices(nb_vertices, vertices);

        index_t expected_tetra = nb_vertices * 7;

        cell_to_v_store_.reserve(expected_tetra * 4);
        cell_to_cell_store_.reserve(expected_tetra * 4);
        cell_next_.reserve(expected_tetra);

        cell_to_v_store_.resize(0);
        cell_to_cell_store_.resize(0);
        cell_next_.resize(0);
        first_free_ = END_OF_LIST;

        //   Sort the vertices spatially. This makes localisation
        // faster.
        if(do_reorder_) {
            compute_BRIO_order(nb_vertices, vertex_ptr(0), reorder_, progressCallback);
        } else {
            reorder_.resize(nb_vertices);
            for(index_t i = 0; i < nb_vertices; ++i) {
                reorder_[i] = i;
            }
        }
        if(progressCallback && !progressCallback(0,0))
        	return false;

        double sorting_time = 0;
        if(benchmark_mode_) {
            sorting_time = W->elapsed_time();
            std::cerr << "BRIO sorting:"
                                       << sorting_time
                                       << std::endl;
        } 

        // The indices of the vertices of the first tetrahedron.
        index_t v0, v1, v2, v3;
        if(!create_first_tetrahedron(v0, v1, v2, v3)) {
        	std::cerr << "All the Delaunay points are coplanar"
                << std::endl;
            return true;
        }

        index_t hint = NO_TETRAHEDRON;
        // Insert all the vertices incrementally.
        for(index_t i = 0; i < nb_vertices; ++i) {
            if(progressCallback && !progressCallback(i,nb_vertices))
            	return false;
            index_t v = reorder_[i];
            // Do not re-insert the first four vertices.
            if(v != v0 && v != v1 && v != v2 && v != v3) {
                index_t new_hint = insert(v, hint);
                if(new_hint != NO_TETRAHEDRON) {
                    hint = new_hint;
                }
            }
        }

        if(benchmark_mode_) {
        	std::cerr << "Core insertion algo:"
                                       << W->elapsed_time() - sorting_time
                                       << std::endl;
        }
        delete W;

        if(debug_mode_) {
            check_combinatorics(verbose_debug_mode_);
            check_geometry(verbose_debug_mode_);
        }

        //   Compress cell_to_v_store_ and cell_to_cell_store_
        // (remove free and virtual tetrahedra).
        //   Since cell_next_ is not used at this point,
        // we reuse it for storing the conversion array that
        // maps old tet indices to new tet indices
        // Note: tet_is_real() uses the previous value of 
        // cell_next(), but we are processing indices
        // in increasing order and since old2new[t] is always
        // smaller or equal to t, we never overwrite a value
        // before needing it.
        
        vector<index_t>& old2new = cell_next_;
        index_t nb_tets = 0;
        index_t nb_tets_to_delete = 0;
        
        {
            for(index_t t = 0; t < max_t(); ++t) {
                if(
                    (keep_infinite_ && !tet_is_free(t)) ||
                    tet_is_real(t)
                ) {
                    if(t != nb_tets) {
                        Memory::copy(
                            &cell_to_v_store_[nb_tets * 4],
                            &cell_to_v_store_[t * 4],
                            4 * sizeof(signed_index_t)
                        );
                        Memory::copy(
                            &cell_to_cell_store_[nb_tets * 4],
                            &cell_to_cell_store_[t * 4],
                            4 * sizeof(signed_index_t)
                        );
                    }
                    old2new[t] = nb_tets;
                    ++nb_tets;
                } else {
                    old2new[t] = index_t(-1);
                    ++nb_tets_to_delete;
                }
            }
            cell_to_v_store_.resize(4 * nb_tets);
            cell_to_cell_store_.resize(4 * nb_tets);
            for(index_t i = 0; i < 4 * nb_tets; ++i) {
                signed_index_t t = cell_to_cell_store_[i];
                geo_debug_assert(t >= 0);
                t = signed_index_t(old2new[t]);
                // Note: t can be equal to -1 when a real tet is
                // adjacent to a virtual one (and this is how the
                // rest of Vorpaline expects to see tets on the
                // border).
                cell_to_cell_store_[i] = t;
            }
        }

        // In "keep_infinite" mode, we reorder the cells in such
        // a way that finite cells have indices [0..nb_finite_cells_-1]
        // and infinite cells have indices [nb_finite_cells_ .. nb_cells_-1]
        
        if(keep_infinite_) {
            nb_finite_cells_ = 0;
            index_t finite_ptr = 0;
            index_t infinite_ptr = nb_tets - 1;
            for(;;) {
                while(tet_is_finite(finite_ptr)) {
                    old2new[finite_ptr] = finite_ptr;
                    ++finite_ptr;
                    ++nb_finite_cells_;
                }
                while(!tet_is_finite(infinite_ptr)) {
                    old2new[infinite_ptr] = infinite_ptr;
                    --infinite_ptr;
                }
                if(finite_ptr > infinite_ptr) {
                    break;
                }
                old2new[finite_ptr] = infinite_ptr;
                old2new[infinite_ptr] = finite_ptr;
                ++nb_finite_cells_;
                for(index_t lf=0; lf<4; ++lf) {
                    geo_swap(
                        cell_to_cell_store_[4*finite_ptr + lf],
                        cell_to_cell_store_[4*infinite_ptr + lf]
                    );
                }
                for(index_t lv=0; lv<4; ++lv) {
                    geo_swap(
                        cell_to_v_store_[4*finite_ptr + lv],
                        cell_to_v_store_[4*infinite_ptr + lv]
                    );
                }
                ++finite_ptr;
                --infinite_ptr;
            }
            for(index_t i = 0; i < 4 * nb_tets; ++i) {
                signed_index_t t = cell_to_cell_store_[i];
                geo_debug_assert(t >= 0);
                t = signed_index_t(old2new[t]);
                geo_debug_assert(t >= 0);
                cell_to_cell_store_[i] = t;
            }
        }

        if(benchmark_mode_) {
            if(keep_infinite_) {
            	std::cerr
                    << "Removed " << nb_tets_to_delete 
                    << " tets (free list)" << std::endl;
            } else {
            	std::cerr
                    << "Removed " << nb_tets_to_delete 
                    << " tets (free list and infinite)" << std::endl;
            }
        }
        
        set_arrays(
            nb_tets,
            cell_to_v_store_.data(), cell_to_cell_store_.data()
        );

        return true;
    }

    index_t Delaunay3d::nearest_vertex(const double* p) const {

        // TODO: For the moment, we fallback to the (unefficient)
        // baseclass implementation when in weighted mode.
        if(weighted_) {
            return Delaunay::nearest_vertex(p);
        }

        // Find a tetrahedron (real or virtual) that contains p
        index_t t = locate(p, NO_TETRAHEDRON, false);

        //   If p is outside the convex hull of the inserted points,
        // a special traversal is required (not implemented yet).
        // TODO: implement convex hull boundary traversal
        // (for now we fallback to linear search implemented
        //  in baseclass)
        if(t == NO_TETRAHEDRON || tet_is_virtual(t)) {
            return Delaunay::nearest_vertex(p);
        }

        double sq_dist = 1e30;
        index_t result = NO_TETRAHEDRON;

        // Find the nearest vertex among t's vertices
        for(index_t lv = 0; lv < 4; ++lv) {
            signed_index_t v = tet_vertex(t, lv);
            // If the tetrahedron is virtual, then the first vertex
            // is the vertex at infinity and is skipped.
            if(v < 0) {
                continue;
            }
            double cur_sq_dist = Geom::distance2(p, vertex_ptr(index_t(v)), 3);
            if(cur_sq_dist < sq_dist) {
                sq_dist = cur_sq_dist;
                result = index_t(v);
            }
        }
        return result;
    }



    index_t Delaunay3d::locate_inexact(
        const double* p, index_t hint, index_t max_iter
    ) const {

        // If no hint specified, find a tetrahedron randomly
        while(hint == NO_TETRAHEDRON) {
            hint = index_t(Numeric::random_int32()) % max_t();
            if(tet_is_free(hint)) {
                hint = NO_TETRAHEDRON;
            }
        }

        //  Always start from a real tet. If the tet is virtual,
        // find its real neighbor (always opposite to the
        // infinite vertex)
        if(tet_is_virtual(hint)) {
            for(index_t lf = 0; lf < 4; ++lf) {
                if(tet_vertex(hint, lf) == VERTEX_AT_INFINITY) {
                    hint = index_t(tet_adjacent(hint, lf));
                    geo_debug_assert(hint != NO_TETRAHEDRON);
                    break;
                }
            }
        }

        index_t t = hint;
        index_t t_pred = NO_TETRAHEDRON;

    still_walking:
        {
            const double* pv[4];
            pv[0] = vertex_ptr(finite_tet_vertex(t,0));
            pv[1] = vertex_ptr(finite_tet_vertex(t,1));
            pv[2] = vertex_ptr(finite_tet_vertex(t,2));
            pv[3] = vertex_ptr(finite_tet_vertex(t,3));
            
            for(index_t f = 0; f < 4; ++f) {
                
                signed_index_t s_t_next = tet_adjacent(t,f);

                //  If the opposite tet is -1, then it means that
                // we are trying to locate() (e.g. called from
                // nearest_vertex) within a tetrahedralization 
                // from which the infinite tets were removed.
                if(s_t_next == -1) {
                    return NO_TETRAHEDRON;
                }

                index_t t_next = index_t(s_t_next);

                //   If the candidate next tetrahedron is the
                // one we came from, then we know already that
                // the orientation is positive, thus we examine
                // the next candidate (or exit the loop if they
                // are exhausted).
                if(t_next == t_pred) {
                    continue ; 
                }

                //   To test the orientation of p w.r.t. the facet f of
                // t, we replace vertex number f with p in t (same
                // convention as in CGAL).
                const double* pv_bkp = pv[f];
                pv[f] = p;
                Sign ori = orient_3d_inexact(pv[0], pv[1], pv[2], pv[3]);

                //   If the orientation is not negative, then we cannot
                // walk towards t_next, and examine the next candidate
                // (or exit the loop if they are exhausted).
                if(ori != NEGATIVE) {
                    pv[f] = pv_bkp;
                    continue;
                }

                //  If the opposite tet is a virtual tet, then
                // the point has a positive orientation relative
                // to the facet on the border of the convex hull,
                // thus t_next is a tet in conflict and we are
                // done.
                if(tet_is_virtual(t_next)) {
                    return t_next;
                }

                //   If we reach this point, then t_next is a valid
                // successor, thus we are still walking.
                t_pred = t;
                t = t_next;
                if(--max_iter != 0) {
                    goto still_walking;
                }
            }
        } 

        //   If we reach this point, we did not find a valid successor
        // for walking (a face for which p has negative orientation), 
        // thus we reached the tet for which p has all positive 
        // face orientations (i.e. the tet that contains p).

        return t;
    }


    index_t Delaunay3d::locate(
        const double* p, index_t hint, bool thread_safe,
        Sign* orient
    ) const {

        //   Try improving the hint by using the 
        // inexact locate function. This gains
        // (a little bit) performance (a few 
        // percent in total Delaunay computation
        // time), but it is better than nothing...
        //   Note: there is a maximum number of tets 
        // traversed by locate_inexact()  (2500)
        // since there exists configurations in which
        // locate_inexact() loops forever !
        hint = locate_inexact(p, hint, 2500);

        static Process::spinlock lock = 0;

        // We need to have this spinlock because
        // of random() that is not thread-safe
        // (TODO: implement a random() function with
        //  thread local storage)
        if(thread_safe) {
            Process::acquire_spinlock(lock);
        }

        // If no hint specified, find a tetrahedron randomly
        while(hint == NO_TETRAHEDRON) {
            hint = index_t(Numeric::random_int32()) % max_t();
            if(tet_is_free(hint)) {
                hint = NO_TETRAHEDRON;
            }
        }

        //  Always start from a real tet. If the tet is virtual,
        // find its real neighbor (always opposite to the
        // infinite vertex)
        if(tet_is_virtual(hint)) {
            for(index_t lf = 0; lf < 4; ++lf) {
                if(tet_vertex(hint, lf) == VERTEX_AT_INFINITY) {
                    hint = index_t(tet_adjacent(hint, lf));
                    geo_debug_assert(hint != NO_TETRAHEDRON);
                    break;
                }
            }
        }

        index_t t = hint;
        index_t t_pred = NO_TETRAHEDRON;
        Sign orient_local[4];
        if(orient == nil) {
            orient = orient_local;
        }


    still_walking:
        {
            const double* pv[4];
            pv[0] = vertex_ptr(finite_tet_vertex(t,0));
            pv[1] = vertex_ptr(finite_tet_vertex(t,1));
            pv[2] = vertex_ptr(finite_tet_vertex(t,2));
            pv[3] = vertex_ptr(finite_tet_vertex(t,3));
            
            // Start from a random facet
            index_t f0 = index_t(Numeric::random_int32()) % 4;
            for(index_t df = 0; df < 4; ++df) {
                index_t f = (f0 + df) % 4;
                
                signed_index_t s_t_next = tet_adjacent(t,f);

                //  If the opposite tet is -1, then it means that
                // we are trying to locate() (e.g. called from
                // nearest_vertex) within a tetrahedralization 
                // from which the infinite tets were removed.
                if(s_t_next == -1) {
                    if(thread_safe) {
                        Process::release_spinlock(lock);
                    }
                    return NO_TETRAHEDRON;
                }

                index_t t_next = index_t(s_t_next);

                //   If the candidate next tetrahedron is the
                // one we came from, then we know already that
                // the orientation is positive, thus we examine
                // the next candidate (or exit the loop if they
                // are exhausted).
                if(t_next == t_pred) {
                    orient[f] = POSITIVE ;
                    continue ; 
                }

                //   To test the orientation of p w.r.t. the facet f of
                // t, we replace vertex number f with p in t (same
                // convention as in CGAL).
                // This is equivalent to tet_facet_point_orient3d(t,f,p)
                // (but less costly, saves a couple of lookups)
                const double* pv_bkp = pv[f];
                pv[f] = p;
                orient[f] = PCK::orient_3d(pv[0], pv[1], pv[2], pv[3]);

                //   If the orientation is not negative, then we cannot
                // walk towards t_next, and examine the next candidate
                // (or exit the loop if they are exhausted).
                if(orient[f] != NEGATIVE) {
                    pv[f] = pv_bkp;
                    continue;
                }

                //  If the opposite tet is a virtual tet, then
                // the point has a positive orientation relative
                // to the facet on the border of the convex hull,
                // thus t_next is a tet in conflict and we are
                // done.
                if(tet_is_virtual(t_next)) {
                    if(thread_safe) {
                        Process::release_spinlock(lock);
                    }
                    for(index_t lf = 0; lf < 4; ++lf) {
                        orient[lf] = POSITIVE;
                    }
                    return t_next;
                }

                //   If we reach this point, then t_next is a valid
                // successor, thus we are still walking.
                t_pred = t;
                t = t_next;
                goto still_walking;
            }
        } 

        //   If we reach this point, we did not find a valid successor
        // for walking (a face for which p has negative orientation), 
        // thus we reached the tet for which p has all positive 
        // face orientations (i.e. the tet that contains p).

        if(thread_safe) {
            Process::release_spinlock(lock);
        }
        return t;
    }

    void Delaunay3d::find_conflict_zone(
        index_t v, 
        index_t t, const Sign* orient, 
        index_t& t_bndry, index_t& f_bndry,
        index_t& first, index_t& last
    ) {
        first = last = END_OF_LIST;

        //  Generate a unique stamp from current vertex index,
        // used for marking tetrahedra.
        set_tet_mark_stamp(v);

        // Pointer to the coordinates of the point to be inserted
        const double* p = vertex_ptr(v);

        geo_debug_assert(t != NO_TETRAHEDRON);

        // Test whether the point already exists in
        // the triangulation. The point already exists
        // if it's located on three faces of the
        // tetrahedron returned by locate().
        int nb_zero = 
            (orient[0] == ZERO) +
            (orient[1] == ZERO) +
            (orient[2] == ZERO) +
            (orient[3] == ZERO) ;

        if(nb_zero >= 3) {
            return; 
        }

        //  Weighted triangulations can have dangling
        // vertices. Such vertices p are characterized by
        // the fact that p is not in conflict with the 
        // tetrahedron returned by locate().
        if(weighted_ && !tet_is_conflict(t, p)) {
            return;
        }

        // Note: points on edges and on facets are
        // handled by the way tet_is_in_conflict()
        // is implemented, that naturally inserts
        // the correct tetrahedra in the conflict list.


        // Mark t as conflict
        add_tet_to_list(t, first, last);

        // A small optimization: if the point to be inserted
        // is on some faces of the located tetrahedron, insert
        // the neighbors accross those faces in the conflict list.
        // It saves a couple of calls to the predicates in this
        // specific case (combinatorics are in general less 
        // expensive than the predicates).
        if(!weighted_ && nb_zero != 0) {
            for(index_t lf = 0; lf < 4; ++lf) {
                if(orient[lf] == ZERO) {
                    index_t t2 = index_t(tet_adjacent(t, lf));
                    add_tet_to_list(t2, first, last);
                }
            }
            for(index_t lf = 0; lf < 4; ++lf) {
                if(orient[lf] == ZERO) {
                    index_t t2 = index_t(tet_adjacent(t, lf));
                    find_conflict_zone_recursive(
                        p,t2,t_bndry,f_bndry,first,last
                    );
                }
            }
        }

        // Determine the conflict list by greedy propagation from t.
        find_conflict_zone_recursive(p,t,t_bndry,f_bndry,first,last);
    }
    
    void Delaunay3d::find_conflict_zone_recursive(
        const double* p, index_t t,
        index_t& t_bndry, index_t& f_bndry,
        index_t& first, index_t& last
    ) {
        for(index_t lf = 0; lf < 4; ++lf) {
            index_t t2 = index_t(tet_adjacent(t, lf));

            if(
                tet_is_in_list(t2) || // known as conflict
                tet_is_marked(t2)     // known as non-conflict
            ) {
                continue;
            }

            if(tet_is_conflict(t2, p)) {
                // Chain t2 in conflict list
                add_tet_to_list(t2, first, last);
                find_conflict_zone_recursive(p,t2,t_bndry,f_bndry,first,last);
                continue;
            } 
                
            //   At this point, t is in conflict 
            // and t2 is not in conflict. 
            // We keep a reference to a tet on the boundary
            t_bndry = t;
            f_bndry = lf;
            // Mark t2 as visited (but not conflict)
            mark_tet(t2);
        }
    }


    index_t Delaunay3d::stellate_conflict_zone(
        index_t v_in, index_t t1, index_t f1, index_t prev_f
    ) {
        
        geo_debug_assert(tet_is_in_list(t1));
        geo_debug_assert(tet_adjacent(t1,f1)>=0);
        geo_debug_assert(!tet_is_in_list(index_t(tet_adjacent(t1,f1))));

        signed_index_t v = signed_index_t(v_in);

        // Create new tetrahedron with same vertices as t_bndry
        index_t new_t = new_tetrahedron(
            tet_vertex(t1,0),
            tet_vertex(t1,1),
            tet_vertex(t1,2),
            tet_vertex(t1,3)
        );

        // Replace in new_t the vertex opposite to f_bndry with v
        set_tet_vertex(new_t, f1, v);

        // Connect new_t with t1's neighbor accross f1
        index_t t2 = index_t(tet_adjacent(t1,f1));
        set_tet_adjacent(new_t, f1, t2);
        set_tet_adjacent(t2, find_tet_adjacent(t2,t1), new_t);
        
        //  Lookup new_t's neighbors accross its three other
        // facets and connect them
        for(index_t new_f=0; new_f<4; ++new_f) {
            if(new_f == prev_f || tet_adjacent(new_t,new_f) != -1) {
                continue;
            }

            //   Find two vertices that are both on facets new_f and f1
            //  (the edge around which we are turning)
            //  This uses duality as follows:
            //  Primal form (not used here): 
            //    halfedge_facet_[v1][v2] returns a facet that is incident
            //    to both v1 and v2.
            //  Dual form (used here):
            //    halfedge_facet_[f1][f2] returns a vertex that both 
            //    f1 and f2 are incident to.
            signed_index_t ev1 = 
                tet_vertex(t1, index_t(halfedge_facet_[new_f][f1]));
            signed_index_t ev2 = 
                tet_vertex(t1, index_t(halfedge_facet_[f1][new_f]));

            //   Turn around edge [ev1,ev2] inside the conflict zone
            // until we reach again the boundary of the conflict zone.
            // Traversing inside the conflict zone is faster (as compared
            // to outside) since it traverses a smaller number of tets.
            index_t cur_t = t1;
            index_t cur_f = new_f;
            index_t next_t = index_t(tet_adjacent(t1,new_f));
            while(tet_is_in_list(next_t)) {
                geo_debug_assert(next_t != t1);
                cur_t = next_t;
                cur_f = get_facet_by_halfedge(cur_t,ev1,ev2);
                next_t = index_t(tet_adjacent(cur_t, cur_f));
            }

            //  At this point, cur_t is in conflict zone and
            // next_t is outside the conflict zone.
            index_t f12,f21;
            get_facets_by_halfedge(next_t, ev1, ev2, f12, f21);
            index_t t_neigh = index_t(tet_adjacent(next_t,f21));
            signed_index_t v_neigh_opposite = tet_vertex(next_t,f12);
            index_t v_neigh_index = find_tet_vertex(
                t_neigh, v_neigh_opposite
            );

            //  Test whether the tet is created, if not 
            // create it (recursive call)
            if(t_neigh == cur_t) {
                t_neigh = stellate_conflict_zone(
                    v_in, t_neigh, cur_f, v_neigh_index
                );
            }
            set_tet_adjacent(t_neigh, v_neigh_index, new_t);
            set_tet_adjacent(new_t, new_f, t_neigh);
        }
        return new_t;
    }


    index_t Delaunay3d::insert(index_t v, index_t hint) {
       index_t t_bndry;
       index_t f_bndry;
       index_t first_conflict, last_conflict;

       const double* p = vertex_ptr(v);

       Sign orient[4];
       index_t t = locate(p, hint, false, orient);
       find_conflict_zone(
           v,t,orient,t_bndry,f_bndry,first_conflict,last_conflict
       );
       
       // The conflict list can be empty if:
       //  - Vertex v already exists in the triangulation
       //  - The triangulation is weighted and v is not visible
       if(first_conflict == END_OF_LIST) {
           return NO_TETRAHEDRON;
       }

       index_t new_tet = stellate_conflict_zone(v,t_bndry,f_bndry);
       
       // Recycle the tetrahedra of the conflict zone.
       cell_next_[last_conflict] = first_free_;
       first_free_ = first_conflict;
       
       // Return one of the newly created tets
       return new_tet;
    }

    bool Delaunay3d::create_first_tetrahedron(
        index_t& iv0, index_t& iv1, index_t& iv2, index_t& iv3
    ) {
        if(nb_vertices() < 4) {
            return false;
        }

        iv0 = 0;

        iv1 = 1;
        while(
            iv1 < nb_vertices() &&
            points_are_identical(
                vertex_ptr(iv0), vertex_ptr(iv1)
            )
        ) {
            ++iv1;
        }
        if(iv1 == nb_vertices()) {
            return false;
        }

        iv2 = iv1 + 1;
        while(
            iv2 < nb_vertices() &&
            points_are_colinear(
                vertex_ptr(iv0), vertex_ptr(iv1), vertex_ptr(iv2)
            )
        ) {
            ++iv2;
        }
        if(iv2 == nb_vertices()) {
            return false;
        }

        iv3 = iv2 + 1;
        Sign s = ZERO;
        while(
            iv3 < nb_vertices() &&
            (s = PCK::orient_3d(
                    vertex_ptr(iv0), vertex_ptr(iv1),
                    vertex_ptr(iv2), vertex_ptr(iv3)
                )) == ZERO
        ) {
            ++iv3;
        }

        if(iv3 == nb_vertices()) {
            return false;
        }

        geo_debug_assert(s != ZERO);

        if(s == NEGATIVE) {
            geo_swap(iv2, iv3);
        }

        // Create the first tetrahedron
        index_t t0 = new_tetrahedron(
            signed_index_t(iv0), 
            signed_index_t(iv1), 
            signed_index_t(iv2), 
            signed_index_t(iv3)
        );

        // Create the first four virtual tetrahedra surrounding it
        index_t t[4];
        for(index_t f = 0; f < 4; ++f) {
            // In reverse order since it is an adjacent tetrahedron
            signed_index_t v1 = tet_vertex(t0, tet_facet_vertex(f,2));
            signed_index_t v2 = tet_vertex(t0, tet_facet_vertex(f,1));
            signed_index_t v3 = tet_vertex(t0, tet_facet_vertex(f,0));
            t[f] = new_tetrahedron(VERTEX_AT_INFINITY, v1, v2, v3);
        }

        // Connect the virtual tetrahedra to the real one
        for(index_t f=0; f<4; ++f) {
            set_tet_adjacent(t[f], 0, t0);
            set_tet_adjacent(t0, f, t[f]);
        }

        // Interconnect the four virtual tetrahedra along their common
        // faces
        for(index_t f = 0; f < 4; ++f) {
            // In reverse order since it is an adjacent tetrahedron
            index_t lv1 = tet_facet_vertex(f,2);
            index_t lv2 = tet_facet_vertex(f,1);
            index_t lv3 = tet_facet_vertex(f,0);
            set_tet_adjacent(t[f], 1, t[lv1]);
            set_tet_adjacent(t[f], 2, t[lv2]);
            set_tet_adjacent(t[f], 3, t[lv3]);
        }

        return true;
    }

    /************************************************************************/

    void Delaunay3d::show_tet(index_t t) const {
        std::cerr << "tet"
            << (tet_is_in_list(t) ? '*' : ' ')
            << t
            << ", v=["
            << tet_vertex(t, 0)
            << ' '
            << tet_vertex(t, 1)
            << ' '
            << tet_vertex(t, 2)
            << ' '
            << tet_vertex(t, 3)
            << "]  adj=[";
        show_tet_adjacent(t, 0);
        show_tet_adjacent(t, 1);
        show_tet_adjacent(t, 2);
        show_tet_adjacent(t, 3);
        std::cerr << "] ";

        for(index_t f = 0; f < 4; ++f) {
            std::cerr << 'f' << f << ':';
            for(index_t v = 0; v < 3; ++v) {
                std::cerr << tet_vertex(t, tet_facet_vertex(f,v))
                    << ',';
            }
            std::cerr << ' ';
        }
        std::cerr << std::endl;
    }

    void Delaunay3d::show_tet_adjacent(index_t t, index_t lf) const {
        signed_index_t adj = tet_adjacent(t, lf);
        if(adj != -1) {
            std::cerr << (tet_is_in_list(index_t(adj)) ? '*' : ' ');
        }
        std::cerr << adj;
        std::cerr << ' ';
    }

    void Delaunay3d::show_list(
        index_t first, const std::string& list_name
    ) const {
        index_t t = first;
        std::cerr << "tet list: " << list_name << std::endl;
        while(t != END_OF_LIST) {
            show_tet(t);
            t = tet_next(t);
        }
        std::cerr << "-------------" << std::endl;
    }

    void Delaunay3d::check_combinatorics(bool verbose) const {
        if(verbose) {
            std::cerr << std::endl;
        }
        bool ok = true;
        std::vector<bool> v_has_tet(nb_vertices(), false);
        for(index_t t = 0; t < max_t(); ++t) {
            if(tet_is_free(t)) {
/*
                if(verbose) {
                    std::cerr << "-Deleted tet: ";
                    show_tet(t);
                }
*/
            } else {
/*
                if(verbose) {
                    std::cerr << "Checking tet: ";
                    show_tet(t);
                }
*/
                for(index_t lf = 0; lf < 4; ++lf) {
                    if(tet_adjacent(t, lf) == -1) {
                        std::cerr << lf << ":Missing adjacent tet"
                            << std::endl;
                        ok = false;
                    } else if(tet_adjacent(t, lf) == signed_index_t(t)) {
                        std::cerr << lf << ":Tet is adjacent to itself"
                            << std::endl;
                        ok = false;
                    } else {
                        index_t t2 = index_t(tet_adjacent(t, lf));
                        bool found = false;
                        for(index_t lf2 = 0; lf2 < 4; ++lf2) {
                            if(tet_adjacent(t2, lf2) == signed_index_t(t)) {
                                found = true;
                            }
                        }
                        if(!found) {
                            std::cerr
                                << lf << ":Adjacent link is not bidirectional"
                                << std::endl;
                            ok = false;
                        }
                    }
                }
                index_t nb_infinite = 0;
                for(index_t lv = 0; lv < 4; ++lv) {
                    if(tet_vertex(t, lv) == -1) {
                        ++nb_infinite;
                    }
                }
                if(nb_infinite > 1) {
                    ok = false;
                    std::cerr << "More than one infinite vertex"
                        << std::endl;
                }
            }
            for(index_t lv = 0; lv < 4; ++lv) {
                signed_index_t v = tet_vertex(t, lv);
                if(v >= 0) {
                    v_has_tet[index_t(v)] = true;
                }
            }
        }
        for(index_t v = 0; v < nb_vertices(); ++v) {
            if(!v_has_tet[v]) {
                if(verbose) {
                    std::cerr << "Vertex " << v
                        << " is isolated (duplicated ?)" << std::endl;
                }
            }
        }
        geo_assert(ok);
        if(verbose) {
            std::cerr << std::endl;
        }
        std::cerr << std::endl << "Delaunay Combi OK" << std::endl;
    }

    void Delaunay3d::check_geometry(bool verbose) const {
        bool ok = true;
        for(index_t t = 0; t < max_t(); ++t) {
            if(!tet_is_free(t)) {
                signed_index_t v0 = tet_vertex(t, 0);
                signed_index_t v1 = tet_vertex(t, 1);
                signed_index_t v2 = tet_vertex(t, 2);
                signed_index_t v3 = tet_vertex(t, 3);
                for(index_t v = 0; v < nb_vertices(); ++v) {
                    signed_index_t sv = signed_index_t(v);
                    if(sv == v0 || sv == v1 || sv == v2 || sv == v3) {
                        continue;
                    }
                    if(tet_is_conflict(t, vertex_ptr(v))) {
                        ok = false;
                        if(verbose) {
                            std::cerr << "Tet " << t <<
                                " is in conflict with vertex " << v
                                    << std::endl;

                            std::cerr << "  offending tet: ";
                            show_tet(v);
                        }
                    }
                }
            }
        }
        geo_assert(ok);
        std::cerr << std::endl << "Delaunay Geo OK" << std::endl;
    }

    /************************************************************************/

    RegularWeightedDelaunay3d::RegularWeightedDelaunay3d(
        coord_index_t dimension
    ) :
        Delaunay3d(4)
    {
        if(dimension != 4) {
            throw InvalidDimension(dimension, "RegularWeightedDelaunay3d", "4");
        }
    }

    RegularWeightedDelaunay3d::~RegularWeightedDelaunay3d() {
    }
}
