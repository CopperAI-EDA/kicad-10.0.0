/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2024 Jon Evans <jon@craftyjon.com>
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <set>
#include <api/api_handler_sch.h>
#include <api/api_sch_utils.h>
#include <api/api_utils.h>
#include <api/schematic/schematic_commands.pb.h>
#include <base_units.h>
#include <eda_item.h>
#include <lib_id.h>
#include <lib_symbol.h>
#include <magic_enum.hpp>
#include <project_sch.h>
#include <connection_graph.h>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <sch_screen.h>
#include <sch_label.h>
#include <sch_line.h>
#include <sch_no_connect.h>
#include <schematic.h>
#include <schematic_settings.h>
#include <sch_sheet.h>
#include <sch_sheet_path.h>
#include <sch_sheet_pin.h>
#include <sch_pin.h>
#include <map>
#include <sch_symbol.h>
#include <symbol.h>
#include <wx/app.h>
#include <wx/tokenzr.h>
#include <libraries/symbol_library_adapter.h>
#include <libraries/library_manager.h>
#include <regex>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <class_draw_panel_gal.h>
#include <eda_draw_frame.h>
#include <frame_type.h>
#include <gal/opengl/opengl_gal.h>
#include <math/vector2wx.h>
#include <view/view.h>
#include <base_units.h>
#include <eeschema_settings.h>
#include <settings/grid_settings.h>
#include <kiway.h>
#include <mail_type.h>
#include <sch_io/sch_io_mgr.h>

#include <api/common/types/base_types.pb.h>
#include <api/api_enums.h>
#include <template_fieldnames.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <sch_io/kicad_sexpr/sch_io_kicad_sexpr.h>
#include <richio.h>

using namespace kiapi::common::commands;
using kiapi::common::types::CommandStatus;
using kiapi::common::types::DocumentType;
using kiapi::common::types::ItemRequestStatus;
using kiapi::schematic::types::AddComponent;
using kiapi::schematic::types::AddComponentResponse;
using kiapi::schematic::types::GetComponentData;
using kiapi::schematic::types::GetComponentDataResponse;
using kiapi::schematic::types::GetDanglingReport;
using kiapi::schematic::types::GetDanglingReportResponse;
using kiapi::schematic::types::CreateNoConnects;
using kiapi::schematic::types::CreateNoConnectsResponse;
using kiapi::schematic::types::CaptureScreenshot;
using kiapi::schematic::types::CaptureScreenshotResponse;
using kiapi::schematic::types::CaptureZoneScreenshot;
using kiapi::schematic::types::CaptureFullSchematic;
using kiapi::schematic::types::GetVisibleBounds;
using kiapi::schematic::types::GetVisibleBoundsResponse;
using kiapi::schematic::types::GetPinPosition;
using kiapi::schematic::types::GetSchematicSummary;
using kiapi::schematic::types::GetSchematicSummaryResponse;
using kiapi::schematic::types::GetNetlist;
using kiapi::schematic::types::GetNetlistResponse;
using kiapi::schematic::types::NetEntry;
using kiapi::schematic::types::NetPinRef;
using kiapi::schematic::types::GetPinPositionResponse;
using kiapi::schematic::types::MoveComponent;
using kiapi::schematic::types::MoveComponentResponse;
using kiapi::schematic::types::SetComponentFootprint;
using kiapi::schematic::types::SetComponentFootprintResponse;
using kiapi::schematic::types::SetComponentFields;
using kiapi::schematic::types::SetComponentFieldsResponse;
using kiapi::schematic::types::DeleteComponent;
using kiapi::schematic::types::DeleteComponentResponse;
using kiapi::schematic::types::ReloadProjectSymbolLibraries;
using kiapi::schematic::types::ReloadProjectSymbolLibrariesResponse;
using kiapi::schematic::types::AppendProjectSymbolLibraryRow;
using kiapi::schematic::types::AppendProjectSymbolLibraryRowResponse;
using kiapi::schematic::types::WriteSymbolLibraryFile;
using kiapi::schematic::types::WriteSymbolLibraryFileResponse;
using kiapi::schematic::types::WriteFootprintLibraryFile;
using kiapi::schematic::types::WriteFootprintLibraryFileResponse;
using kiapi::schematic::types::SearchSymbols;
using kiapi::schematic::types::SearchSymbolsResponse;
using kiapi::schematic::types::SymbolSearchResult;
using kiapi::schematic::types::NavigateToSheet;
using kiapi::schematic::types::NavigateToSheetResponse;
using kiapi::schematic::types::CreateBlockSheet;
using kiapi::schematic::types::CreateBlockSheetResponse;
using kiapi::schematic::types::AddSheetPort;
using kiapi::schematic::types::AddSheetPortResponse;
using kiapi::schematic::types::GetHierarchy;
using kiapi::schematic::types::GetHierarchyResponse;
using kiapi::schematic::types::HierarchySheetInfo;
using kiapi::schematic::types::HierarchyPortInfo;
using kiapi::schematic::types::MoveSheet;
using kiapi::schematic::types::MoveSheetResponse;
using kiapi::schematic::types::DeleteSheet;
using kiapi::schematic::types::DeleteSheetResponse;
using kiapi::schematic::types::MoveSheetPort;
using kiapi::schematic::types::MoveSheetPortResponse;
using kiapi::schematic::types::DeleteSheetPort;
using kiapi::schematic::types::DeleteSheetPortResponse;
using kiapi::common::commands::SaveDocument;
using kiapi::common::commands::RevertDocument;


namespace
{
void broadcastSymbolLibraryReload( SCH_EDIT_FRAME* aFrame )
{
    wxCHECK( aFrame, /*void*/ );

    std::string payload;
    aFrame->Kiway().ExpressMail( FRAME_SCH, MAIL_RELOAD_LIB, payload );
    aFrame->Kiway().ExpressMail( FRAME_SCH_SYMBOL_EDITOR, MAIL_RELOAD_LIB, payload );
    aFrame->Kiway().ExpressMail( FRAME_SCH_VIEWER, MAIL_RELOAD_LIB, payload );
}
}


API_HANDLER_SCH::API_HANDLER_SCH( SCH_EDIT_FRAME* aFrame ) :
        API_HANDLER_EDITOR( aFrame ),
        m_frame( aFrame )
{
    registerHandler<GetOpenDocuments, GetOpenDocumentsResponse>(
            &API_HANDLER_SCH::handleGetOpenDocuments );
    registerHandler<SearchSymbols, SearchSymbolsResponse>( &API_HANDLER_SCH::handleSearchSymbols );
    registerHandler<GetComponentData, GetComponentDataResponse>(
            &API_HANDLER_SCH::handleGetComponentData );
    registerHandler<AddComponent, AddComponentResponse>( &API_HANDLER_SCH::handleAddComponent );
    registerHandler<GetPinPosition, GetPinPositionResponse>( &API_HANDLER_SCH::handleGetPinPosition );
    registerHandler<GetDanglingReport, GetDanglingReportResponse>(
            &API_HANDLER_SCH::handleGetDanglingReport );
    registerHandler<CreateNoConnects, CreateNoConnectsResponse>(
            &API_HANDLER_SCH::handleCreateNoConnects );
    registerHandler<GetSchematicSummary, GetSchematicSummaryResponse>(
            &API_HANDLER_SCH::handleGetSchematicSummary );
    registerHandler<GetNetlist, GetNetlistResponse>( &API_HANDLER_SCH::handleGetNetlist );
    registerHandler<CaptureScreenshot, CaptureScreenshotResponse>(
            &API_HANDLER_SCH::handleCaptureScreenshot );
    registerHandler<CaptureZoneScreenshot, CaptureScreenshotResponse>(
            &API_HANDLER_SCH::handleCaptureZoneScreenshot );
    registerHandler<CaptureFullSchematic, CaptureScreenshotResponse>(
            &API_HANDLER_SCH::handleCaptureFullSchematic );
    registerHandler<GetVisibleBounds, GetVisibleBoundsResponse>(
            &API_HANDLER_SCH::handleGetVisibleBounds );
    registerHandler<MoveComponent, MoveComponentResponse>( &API_HANDLER_SCH::handleMoveComponent );
    registerHandler<SetComponentFootprint, SetComponentFootprintResponse>(
            &API_HANDLER_SCH::handleSetComponentFootprint );
    registerHandler<SetComponentFields, SetComponentFieldsResponse>(
            &API_HANDLER_SCH::handleSetComponentFields );
    registerHandler<DeleteComponent, DeleteComponentResponse>(
            &API_HANDLER_SCH::handleDeleteComponent );
    registerHandler<ReloadProjectSymbolLibraries, ReloadProjectSymbolLibrariesResponse>(
            &API_HANDLER_SCH::handleReloadProjectSymbolLibraries );
    registerHandler<AppendProjectSymbolLibraryRow, AppendProjectSymbolLibraryRowResponse>(
            &API_HANDLER_SCH::handleAppendProjectSymbolLibraryRow );
    registerHandler<WriteSymbolLibraryFile, WriteSymbolLibraryFileResponse>(
            &API_HANDLER_SCH::handleWriteSymbolLibraryFile );
    registerHandler<WriteFootprintLibraryFile, WriteFootprintLibraryFileResponse>(
            &API_HANDLER_SCH::handleWriteFootprintLibraryFile );
    registerHandler<GetItems, GetItemsResponse>( &API_HANDLER_SCH::handleGetItems );
    registerHandler<SaveDocumentToString, SavedDocumentResponse>(
            &API_HANDLER_SCH::handleSaveDocumentToString );
    registerHandler<SaveDocument, Empty>( &API_HANDLER_SCH::handleSaveDocument );
    registerHandler<RevertDocument, Empty>( &API_HANDLER_SCH::handleRevertDocument );
    registerHandler<NavigateToSheet, NavigateToSheetResponse>(
            &API_HANDLER_SCH::handleNavigateToSheet );
    registerHandler<CreateBlockSheet, CreateBlockSheetResponse>(
            &API_HANDLER_SCH::handleCreateBlockSheet );
    registerHandler<AddSheetPort, AddSheetPortResponse>(
            &API_HANDLER_SCH::handleAddSheetPort );
    registerHandler<GetHierarchy, GetHierarchyResponse>(
            &API_HANDLER_SCH::handleGetHierarchy );
    registerHandler<MoveSheet, MoveSheetResponse>(
            &API_HANDLER_SCH::handleMoveSheet );
    registerHandler<DeleteSheet, DeleteSheetResponse>(
            &API_HANDLER_SCH::handleDeleteSheet );
    registerHandler<MoveSheetPort, MoveSheetPortResponse>(
            &API_HANDLER_SCH::handleMoveSheetPort );
    registerHandler<DeleteSheetPort, DeleteSheetPortResponse>(
            &API_HANDLER_SCH::handleDeleteSheetPort );
}


std::unique_ptr<COMMIT> API_HANDLER_SCH::createCommit()
{
    return std::make_unique<SCH_COMMIT>( m_frame );
}


void API_HANDLER_SCH::pushCurrentCommit( const std::string& aClientName,
                                         const wxString& aMessage )
{
    auto it = m_commits.find( aClientName );

    if( it == m_commits.end() )
        return;

    std::deque<EDA_ITEM*> addedConnectableItems;

    for( EDA_ITEM* item : it->second.second->GetStagedAdds() )
    {
        SCH_ITEM* schItem = dynamic_cast<SCH_ITEM*>( item );

        if( schItem && schItem->IsConnectable() )
            addedConnectableItems.push_back( schItem );
    }

    API_HANDLER_EDITOR::pushCurrentCommit( aClientName, aMessage );

    if( addedConnectableItems.empty() )
        return;

    SCH_SCREEN* screen = m_frame->GetScreen();

    if( !screen )
        return;

    SCH_COMMIT cleanupCommit( m_frame );

    for( const VECTOR2I& point : screen->GetNeededJunctions( addedConnectableItems ) )
        m_frame->AddJunction( &cleanupCommit, screen, point );

    m_frame->Schematic().CleanUp( &cleanupCommit, screen );

    if( !cleanupCommit.Empty() )
        cleanupCommit.Push( _( "Fixup API schematic connectivity" ) );
}


COMMIT* API_HANDLER_SCH::getCommitByIdOrImplicit( const std::string& aCommitId,
                                                  const std::string& aClientName,
                                                  bool& aImplicitCommit )
{
    aImplicitCommit = false;

    for( auto& it : m_commits )
    {
        if( it.second.first.AsStdString() == aCommitId )
            return it.second.second.get();
    }

    if( m_activeClients.count( aClientName ) )
        return nullptr;

    aImplicitCommit = true;
    return getCurrentCommit( aClientName );
}


void API_HANDLER_SCH::pushImplicitCommit( bool aImplicitCommit,
                                          const std::string& aClientName,
                                          const wxString& aMessage )
{
    if( aImplicitCommit )
        pushCurrentCommit( aClientName, aMessage );
}


bool API_HANDLER_SCH::validateDocumentInternal( const DocumentSpecifier& aDocument ) const
{
    if( aDocument.type() != DocumentType::DOCTYPE_SCHEMATIC )
        return false;

    // TODO(JE) need serdes for SCH_SHEET_PATH <> SheetPath
    return true;

    //wxString currentPath = m_frame->GetCurrentSheet().PathAsString();
    //return 0 == aDocument.sheet_path().compare( currentPath.ToStdString() );
}


HANDLER_RESULT<GetOpenDocumentsResponse> API_HANDLER_SCH::handleGetOpenDocuments(
        const HANDLER_CONTEXT<GetOpenDocuments>& aCtx )
{
    if( aCtx.Request.type() != DocumentType::DOCTYPE_SCHEMATIC )
    {
        ApiResponseStatus e;

        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }

    GetOpenDocumentsResponse response;
    common::types::DocumentSpecifier doc;

    wxFileName fn( m_frame->GetCurrentFileName() );

    doc.set_type( DocumentType::DOCTYPE_SCHEMATIC );

    // Report the full absolute path, not just the basename — the API client
    // (which may run in a separate container) cannot resolve a bare filename.
    doc.set_board_filename( fn.GetFullPath().ToUTF8() );

    // Populate the project specifier so the client can resolve the project root
    // without guessing paths.  Empty for never-saved (null) projects.
    if( !m_frame->Prj().IsNullProject() )
    {
        doc.mutable_project()->set_path( m_frame->Prj().GetProjectPath().ToUTF8() );
        doc.mutable_project()->set_name( m_frame->Prj().GetProjectName().ToUTF8() );
    }

    response.mutable_documents()->Add( std::move( doc ) );
    return response;
}


HANDLER_RESULT<std::unique_ptr<EDA_ITEM>> API_HANDLER_SCH::createItemForType( KICAD_T aType,
        EDA_ITEM* aContainer )
{
    // Only require a container for item types that must be inside another item
    const bool needsContainer = ( aType == SCH_PIN_T || aType == SCH_SHEET_PIN_T
                                  || aType == SCH_FIELD_T );
    if( needsContainer && !aContainer )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "Tried to create an item in a null container" );
        return tl::unexpected( e );
    }

    if( aType == SCH_PIN_T && aContainer && !dynamic_cast<SCH_SYMBOL*>( aContainer ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create a pin in {}, which is not a symbol",
                                          aContainer->GetFriendlyName().ToStdString() ) );
        return tl::unexpected( e );
    }
    else if( aType == SCH_SYMBOL_T )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "CreateItems cannot create schematic symbols; use AddComponent" );
        return tl::unexpected( e );
    }

    std::unique_ptr<EDA_ITEM> created = CreateItemForType( aType, aContainer );

    if( !created )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "Tried to create an item of type {}, which is unhandled",
                                          magic_enum::enum_name( aType ) ) );
        return tl::unexpected( e );
    }

    return created;
}


HANDLER_RESULT<ItemRequestStatus> API_HANDLER_SCH::handleCreateUpdateItemsInternal( bool aCreate,
        const std::string& aClientName,
        const types::ItemHeader &aHeader,
        const google::protobuf::RepeatedPtrField<google::protobuf::Any>& aItems,
        std::function<void( ItemStatus, google::protobuf::Any )> aItemHandler )
{
    ApiResponseStatus e;

    auto containerResult = validateItemHeaderDocument( aHeader );

    if( !containerResult && containerResult.error().status() == ApiStatusCode::AS_UNHANDLED )
    {
        // No message needed for AS_UNHANDLED; this is an internal flag for the API server
        e.set_status( ApiStatusCode::AS_UNHANDLED );
        return tl::unexpected( e );
    }
    else if( !containerResult )
    {
        e.CopyFrom( containerResult.error() );
        return tl::unexpected( e );
    }

    SCH_SCREEN* screen = m_frame->GetScreen();
    EE_RTREE& screenItems = screen->Items();

    std::map<KIID, EDA_ITEM*> itemUuidMap;

    std::for_each( screenItems.begin(), screenItems.end(),
                   [&]( EDA_ITEM* aItem )
                   {
                       itemUuidMap[aItem->m_Uuid] = aItem;
                   } );

    EDA_ITEM* container = nullptr;

    if( containerResult->has_value() )
    {
        const KIID& containerId = **containerResult;

        if( itemUuidMap.count( containerId ) )
        {
            container = itemUuidMap.at( containerId );

            if( !container )
            {
                e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                e.set_error_message( fmt::format(
                        "The requested container {} is not a valid schematic item container",
                        containerId.AsStdString() ) );
                return tl::unexpected( e );
            }
        }
        else
        {
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format(
                    "The requested container {} does not exist in this document",
                    containerId.AsStdString() ) );
            return tl::unexpected( e );
        }
    }

    COMMIT* commit = getCurrentCommit( aClientName );

    for( const google::protobuf::Any& anyItem : aItems )
    {
        ItemStatus status;
        std::optional<KICAD_T> type = TypeNameFromAny( anyItem );

        if( !type )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
            status.set_error_message( fmt::format( "Could not decode a valid type from {}",
                                                   anyItem.type_url() ) );
            aItemHandler( status, anyItem );
            continue;
        }

        HANDLER_RESULT<std::unique_ptr<EDA_ITEM>> creationResult =
                createItemForType( *type, container );

        if( !creationResult )
        {
            status.set_code( ItemStatusCode::ISC_INVALID_TYPE );
            status.set_error_message( creationResult.error().error_message() );
            aItemHandler( status, anyItem );
            continue;
        }

        std::unique_ptr<EDA_ITEM> item( std::move( *creationResult ) );

        if( !item->Deserialize( anyItem ) )
        {
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "could not unpack {} from request",
                                              item->GetClass().ToStdString() ) );
            return tl::unexpected( e );
        }

        if( aCreate && itemUuidMap.count( item->m_Uuid ) )
        {
            status.set_code( ItemStatusCode::ISC_EXISTING );
            status.set_error_message( fmt::format( "an item with UUID {} already exists",
                                                   item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }
        else if( !aCreate && !itemUuidMap.count( item->m_Uuid ) )
        {
            status.set_code( ItemStatusCode::ISC_NONEXISTENT );
            status.set_error_message( fmt::format( "an item with UUID {} does not exist",
                                                   item->m_Uuid.AsStdString() ) );
            aItemHandler( status, anyItem );
            continue;
        }

        status.set_code( ItemStatusCode::ISC_OK );
        google::protobuf::Any newItem;

        if( aCreate )
        {
            item->Serialize( newItem );
            commit->Add( item.release(), screen );

            if( !m_activeClients.count( aClientName ) )
                pushCurrentCommit( aClientName, _( "Added items via API" ) );
        }
        else
        {
            EDA_ITEM* edaItem = itemUuidMap[item->m_Uuid];

            if( SCH_ITEM* schItem = dynamic_cast<SCH_ITEM*>( edaItem ) )
            {
                schItem->SwapItemData( static_cast<SCH_ITEM*>( item.get() ) );
                schItem->Serialize( newItem );
                commit->Modify( schItem, screen );
            }
            else
            {
                wxASSERT( false );
            }

            if( !m_activeClients.count( aClientName ) )
                pushCurrentCommit( aClientName, _( "Created items via API" ) );
        }

        aItemHandler( status, newItem );
    }


    return ItemRequestStatus::IRS_OK;
}


void API_HANDLER_SCH::deleteItemsInternal( std::map<KIID, ItemDeletionStatus>& aItemsToDelete,
                                           const std::string& aClientName )
{
    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
        return;

    std::vector<SCH_ITEM*> toRemove;
    for( std::pair<const KIID, ItemDeletionStatus>& pair : aItemsToDelete )
    {
        for( EDA_ITEM* item : screen->Items() )
        {
            if( item->m_Uuid == pair.first )
            {
                toRemove.push_back( static_cast<SCH_ITEM*>( item ) );
                pair.second = ItemDeletionStatus::IDS_OK;
                break;
            }
        }
    }

    COMMIT* commit = getCurrentCommit( aClientName );
    for( SCH_ITEM* item : toRemove )
    {
        // Stage with the screen: SCH_COMMIT::pushSchEdits drops entries whose
        // m_screen is null (wxCHECK2 -> continue), so the one-arg Remove made
        // every API-driven deletion a silent no-op.
        commit->Remove( item, screen );
    }

    if( !m_activeClients.count( aClientName ) )
        pushCurrentCommit( aClientName, _( "Deleted items via API" ) );
}


std::optional<EDA_ITEM*> API_HANDLER_SCH::getItemFromDocument( const DocumentSpecifier& aDocument,
                                                               const KIID& aId )
{
    if( !validateDocument( aDocument ) )
        return std::nullopt;

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
        return std::nullopt;

    for( EDA_ITEM* item : screen->Items() )
    {
        if( item->m_Uuid == aId )
            return item;
    }
    return std::nullopt;
}


HANDLER_RESULT<SearchSymbolsResponse> API_HANDLER_SCH::handleSearchSymbols(
        const HANDLER_CONTEXT<SearchSymbols>& aCtx )
{
    SearchSymbolsResponse response;

    SYMBOL_LIBRARY_ADAPTER* libTable = PROJECT_SCH::SymbolLibAdapter( &m_frame->Prj() );
    if( !libTable )
        return response;

    wxString query = wxString( aCtx.Request.query().c_str(), wxConvUTF8 ).Trim();
    wxString targetLib = wxString( aCtx.Request.library().c_str(), wxConvUTF8 );
    int limit = aCtx.Request.limit() > 0 ? aCtx.Request.limit() : 100;

    // Ripgrep-style: split query into tokens (space, comma, etc.); match if ANY token matches
    std::vector<wxString> queryTokens;
    if( !query.IsEmpty() )
    {
        wxStringTokenizer tokenizer( query, wxS( " ,;\t\n" ), wxTOKEN_STRTOK );
        while( tokenizer.HasMoreTokens() )
        {
            wxString t = tokenizer.GetNextToken().Lower();
            if( !t.IsEmpty() )
                queryTokens.push_back( t );
        }
    }

    std::vector<wxString> libs;
    if( !targetLib.IsEmpty() && libTable->HasLibrary( targetLib, true ) )
        libs.push_back( targetLib );
    else if( targetLib.IsEmpty() )
        libs = libTable->GetLibraryNames();

    for( const wxString& libNickname : libs )
    {
        if( (int) response.results_size() >= limit )
            break;

        wxArrayString aliasNames;
        try
        {
            for( const wxString& n : libTable->GetSymbolNames( libNickname ) )
                aliasNames.Add( n );
        }
        catch( const IO_ERROR& )
        {
            continue;
        }

        for( size_t i = 0; i < aliasNames.GetCount() && (int) response.results_size() < limit; i++ )
        {
            wxString name = aliasNames[i];

            LIB_SYMBOL* symbol = nullptr;
            try
            {
                symbol = libTable->LoadSymbol( libNickname, name );
            }
            catch( const IO_ERROR& )
            {
                continue;
            }

            if( !symbol )
                continue;

            // Ripgrep-style: if query has tokens, match if ANY token is in name, description, or keywords
            if( !queryTokens.empty() )
            {
                wxString nameLower = name.Lower();
                wxString descLower = symbol->GetDescription().Lower();
                wxString kwLower = symbol->GetKeyWords().Lower();
                bool anyMatch = false;
                for( const wxString& tok : queryTokens )
                {
                    if( nameLower.Contains( tok ) || descLower.Contains( tok ) || kwLower.Contains( tok ) )
                    {
                        anyMatch = true;
                        break;
                    }
                }
                if( !anyMatch )
                    continue;
            }

            SymbolSearchResult* result = response.add_results();
            result->set_library_nickname( libNickname.ToStdString() );
            result->set_symbol_name( name.ToStdString() );
            result->set_description( symbol->GetDescription().ToStdString() );
            result->set_keywords( symbol->GetKeyWords().ToStdString() );
            result->set_datasheet( symbol->GetDatasheetField().GetText().ToStdString() );
        }
    }

    return response;
}


HANDLER_RESULT<GetComponentDataResponse> API_HANDLER_SCH::handleGetComponentData(
        const HANDLER_CONTEXT<GetComponentData>& aCtx )
{
    GetComponentDataResponse response;

    SYMBOL_LIBRARY_ADAPTER* libTable = PROJECT_SCH::SymbolLibAdapter( &m_frame->Prj() );
    if( !libTable )
        return response;

    if( aCtx.Request.has_lib_id() )
    {
        wxString libNickname( aCtx.Request.lib_id().library_nickname().c_str(), wxConvUTF8 );
        wxString entryName( aCtx.Request.lib_id().entry_name().c_str(), wxConvUTF8 );
        LIB_SYMBOL* symbol = nullptr;
        try
        {
            symbol = libTable->LoadSymbol( libNickname, entryName );
        }
        catch( const IO_ERROR& )
        {
            ApiResponseStatus err;
            err.set_status( ApiStatusCode::AS_BAD_REQUEST );
            err.set_error_message( "Library symbol not found" );
            return tl::unexpected( err );
        }
        response.set_library_nickname( libNickname.ToStdString() );
        response.set_symbol_name( entryName.ToStdString() );
        if( symbol )
        {
            response.set_description( symbol->GetDescription().ToStdString() );
            response.set_keywords( symbol->GetKeyWords().ToStdString() );
            response.set_datasheet( symbol->GetDatasheetField().GetText().ToStdString() );
            response.set_summary( "Library symbol: " + libNickname.ToStdString() + ":"
                                 + entryName.ToStdString() + " - "
                                 + symbol->GetDescription().ToStdString() );
            response.set_unit_count( symbol->GetUnitCount() > 0 ? symbol->GetUnitCount() : 1 );
            BOX2I bbox = symbol->GetUnitBoundingBox( 0, 0 );
            double wMm = std::max<long long>( 0LL, static_cast<long long>( bbox.GetWidth() ) ) / SCH_IU_PER_MM;
            double hMm = std::max<long long>( 0LL, static_cast<long long>( bbox.GetHeight() ) ) / SCH_IU_PER_MM;
            if( wMm > 0 && hMm > 0 )
            {
                response.set_width_mm( wMm );
                response.set_height_mm( hMm );
            }
            // Predefined sizes for Device passives (override empty bbox or enforce consistency)
            std::string lib( libNickname.ToStdString() );
            std::string symName( entryName.ToStdString() );
            if( lib == "Device" )
            {
                if( symName == "R" ) { response.set_width_mm( 5.0 ); response.set_height_mm( 2.0 ); }
                else if( symName == "C" ) { response.set_width_mm( 4.0 ); response.set_height_mm( 2.0 ); }
                else if( symName == "L" ) { response.set_width_mm( 4.0 ); response.set_height_mm( 2.0 ); }
                else if( symName == "LED" ) { response.set_width_mm( 4.0 ); response.set_height_mm( 2.0 ); }
                else if( symName == "Ferrite_Bead" ) { response.set_width_mm( 4.0 ); response.set_height_mm( 2.0 ); }
            }
            // Pin list for library symbols (unit 0, body 0) so the agent can wire by pin number/name
            for( SCH_PIN* pin : symbol->GetPins() )
            {
                auto* ps = response.add_pins();
                ps->set_number( pin->GetNumber().ToStdString() );
                ps->set_name( pin->GetName().ToStdString() );
            }
        }
        else
        {
            response.set_summary( "Library symbol " + libNickname.ToStdString() + ":"
                                 + entryName.ToStdString() + " not found" );
        }
    }
    else if( aCtx.Request.has_component_id() && !aCtx.Request.component_id().value().empty() )
    {
        KIID kiid( aCtx.Request.component_id().value() );
        SCH_SCREEN* screen = m_frame->GetScreen();
        if( screen )
        {
            for( auto it = screen->Items().begin(); it != screen->Items().end(); ++it )
            {
                SCH_ITEM* item = *it;
                if( item->m_Uuid == kiid && SCH_SYMBOL::ClassOf( item ) )
                {
                    SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );
                    std::string summary = "Schematic symbol: "
                                         + sym->GetRef( &m_frame->GetCurrentSheet() ).ToStdString()
                                         + " "
                                         + sym->GetValue( false, &m_frame->GetCurrentSheet(), false )
                                                   .ToStdString()
                                         + " "
                                         + sym->GetLibId().GetUniStringLibId().ToStdString();
                    response.set_summary( summary );
                    response.set_unit_count( sym->GetUnitCount() > 0 ? sym->GetUnitCount() : 1 );
                    BOX2I bbox = sym->GetBodyBoundingBox();
                    double wMm = std::max<long long>( 0LL, static_cast<long long>( bbox.GetWidth() ) ) / SCH_IU_PER_MM;
                    double hMm = std::max<long long>( 0LL, static_cast<long long>( bbox.GetHeight() ) ) / SCH_IU_PER_MM;
                    if( wMm > 0 && hMm > 0 )
                    {
                        response.set_width_mm( wMm );
                        response.set_height_mm( hMm );
                    }
                    const SCH_SHEET_PATH& sheet = m_frame->GetCurrentSheet();
                    for( SCH_PIN* pin : sym->GetPins( &sheet ) )
                    {
                        auto* ps = response.add_pins();
                        ps->set_number( pin->GetNumber().ToStdString() );
                        ps->set_name( pin->GetName().ToStdString() );
                    }
                    break;
                }
            }
        }
    }

    return response;
}


HANDLER_RESULT<AddComponentResponse> API_HANDLER_SCH::handleAddComponent(
        const HANDLER_CONTEXT<AddComponent>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const AddComponent& req = aCtx.Request;
    std::string commitIdStr = req.commit_id().value();

    LIB_ID libId( wxString( req.library_nickname().c_str(), wxConvUTF8 ),
                  wxString( req.symbol_name().c_str(), wxConvUTF8 ) );
    SYMBOL_LIBRARY_ADAPTER* libTable = PROJECT_SCH::SymbolLibAdapter( &m_frame->Prj() );
    if( !libTable )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No symbol library table" );
        return tl::unexpected( err );
    }

    LIB_SYMBOL* libSymbol = libTable->LoadSymbol( libId );
    if( !libSymbol )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "Symbol not found in library" );
        return tl::unexpected( err );
    }

    double xMm = req.has_position() ? req.position().x_mm() : 0.0;
    double yMm = req.has_position() ? req.position().y_mm() : 0.0;
    VECTOR2I posIU( KiROUND( xMm * SCH_IU_PER_MM ), KiROUND( yMm * SCH_IU_PER_MM ) );

    // Multi-unit symbols (op-amps, FPGAs split into banks, ...) share one
    // reference across units U?A/U?B/...; req.unit() is the 1-based unit to
    // place. 0 defaults to the first unit; clamp into [1, unitCount].
    const int unitCount = libSymbol->GetUnitCount() > 0 ? libSymbol->GetUnitCount() : 1;
    int unit = req.unit() <= 0 ? 1 : req.unit();
    if( unit > unitCount )
        unit = unitCount;

    const SCH_SHEET_PATH& currentSheet = m_frame->GetCurrentSheet();
    SCH_SYMBOL* symbol = new SCH_SYMBOL( *libSymbol, libId, &currentSheet, unit, 0, posIU, nullptr );

    symbol->SetRef( &currentSheet, wxString( req.reference().c_str(), wxConvUTF8 ) );
    symbol->SetUnitSelection( &currentSheet, unit );
    symbol->SetUnit( unit );
    symbol->SetValueFieldText( wxString( req.value().c_str(), wxConvUTF8 ) );

    double rotDeg = req.rotation();
    int orient = SYM_ORIENT_0;
    if( rotDeg >= 45 && rotDeg < 135 )
        orient = SYM_ORIENT_90;
    else if( rotDeg >= 135 && rotDeg < 225 )
        orient = SYM_ORIENT_180;
    else if( rotDeg >= 225 && rotDeg < 315 )
        orient = SYM_ORIENT_270;
    symbol->SetOrientation( orient );

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
    {
        delete symbol;
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No schematic screen" );
        return tl::unexpected( err );
    }

    bool implicitCommit = false;
    COMMIT* commit = getCommitByIdOrImplicit( commitIdStr, aCtx.ClientName, implicitCommit );
    if( !commit )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "Invalid or expired commit ID" );
        return tl::unexpected( err );
    }

    commit->Add( symbol, screen );
    pushImplicitCommit( implicitCommit, aCtx.ClientName, _( "Added component via API" ) );

    AddComponentResponse resp;
    resp.mutable_component_id()->set_value( symbol->m_Uuid.AsStdString() );
    resp.set_unit( unit );
    resp.set_unit_count( unitCount );
    return resp;
}


HANDLER_RESULT<GetPinPositionResponse> API_HANDLER_SCH::handleGetPinPosition(
        const HANDLER_CONTEXT<GetPinPosition>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const GetPinPosition& req = aCtx.Request;
    if( req.reference().empty() || req.pin_number().empty() )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "reference and pin_number are required" );
        return tl::unexpected( err );
    }

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No schematic open" );
        return tl::unexpected( err );
    }

    const SCH_SHEET_PATH& sheet = m_frame->GetCurrentSheet();
    wxString refReq( req.reference().c_str(), wxConvUTF8 );
    wxString pinNumReq( req.pin_number().c_str(), wxConvUTF8 );

    // Multi-unit parts (op-amp, FPGA banks, ...) place several SCH_SYMBOLs that
    // SHARE one reference, each owning the pins of its own unit. Resolving by
    // reference alone and stopping at the first match would look for the pin on
    // the wrong unit, so prefer the symbol whose unit actually owns the
    // requested pin; fall back to the first ref match (single-unit / not-found
    // error path).
    SCH_SYMBOL* symbol = nullptr;
    SCH_SYMBOL* refFallback = nullptr;
    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() != SCH_SYMBOL_T )
            continue;
        SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );
        if( sym->GetRef( &sheet ) != refReq )
            continue;
        if( !refFallback )
            refFallback = sym;
        if( sym->GetPin( pinNumReq ) )   // this unit owns the requested pin
        {
            symbol = sym;
            break;
        }
    }
    if( !symbol )
        symbol = refFallback;

    // If not on screen, look in pending commits (e.g. symbol just placed in same commit)
    if( !symbol )
    {
        SCH_SYMBOL* stagedFallback = nullptr;
        for( auto& it : m_commits )
        {
            std::vector<EDA_ITEM*> staged = it.second.second->GetStagedAdds();
            for( EDA_ITEM* item : staged )
            {
                if( item->Type() != SCH_SYMBOL_T )
                    continue;
                SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );
                if( sym->GetRef( &sheet ) != refReq )
                    continue;
                // Ensure pins are resolved from library (m_position, m_libPin) so
                // GetPosition/GetPinRoot are per-pin, and GetPin can match.
                sym->UpdatePins();
                if( !stagedFallback )
                    stagedFallback = sym;
                if( sym->GetPin( pinNumReq ) )
                {
                    symbol = sym;
                    break;
                }
            }
            if( symbol )
                break;
        }
        if( !symbol )
            symbol = stagedFallback;
    }

    if( !symbol )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "Symbol not found: " + req.reference() );
        return tl::unexpected( err );
    }

    SCH_PIN* pin = symbol->GetPin( pinNumReq );
    if( !pin )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "Pin not found: " + req.reference() + " pin " + req.pin_number() );
        return tl::unexpected( err );
    }

    // Prefer library pin positions (source of truth) so pin positions are correct even when
    // the symbol is in a pending commit and schematic pins are not fully resolved.
    VECTOR2I posIU;
    VECTOR2I tipIU;
    if( symbol->GetLibSymbolRef() )
    {
        LIB_SYMBOL* libPart = symbol->GetLibSymbolRef().get();
        const SCH_PIN* libPin = libPart->GetPin( pinNumReq, symbol->GetUnit(), symbol->GetBodyStyle() );
        if( libPin )
        {
            const TRANSFORM& t = symbol->GetTransform();
            VECTOR2I          symPos = symbol->GetPosition();
            posIU = t.TransformCoordinate( libPin->GetPosition() ) + symPos;
            tipIU = t.TransformCoordinate( libPin->GetPinRoot() ) + symPos;
        }
        else
        {
            posIU = pin->GetPosition();
            tipIU = pin->GetPinRoot();
        }
    }
    else
    {
        posIU = pin->GetPosition();
        tipIU = pin->GetPinRoot();
    }

    wxLogDebug( "GetPinPosition ref=%s pin=%s symPos=(%d,%d) posIU=(%d,%d) tipIU=(%d,%d) hasLibRef=%d",
                req.reference(), req.pin_number(),
                symbol->GetPosition().x, symbol->GetPosition().y,
                posIU.x, posIU.y, tipIU.x, tipIU.y,
                (int)( symbol->GetLibSymbolRef() != nullptr ) );

    GetPinPositionResponse resp;
    resp.mutable_position()->set_x_mm( posIU.x / SCH_IU_PER_MM );
    resp.mutable_position()->set_y_mm( posIU.y / SCH_IU_PER_MM );
    // Orientation for label placement: 0=right, 90=up, 180=left, 270=down
    PIN_ORIENTATION orient = pin->PinDrawOrient( symbol->GetTransform() );
    double orientDeg = 0;
    switch( orient )
    {
        case PIN_ORIENTATION::PIN_RIGHT: orientDeg = 0; break;
        case PIN_ORIENTATION::PIN_UP:    orientDeg = 90; break;
        case PIN_ORIENTATION::PIN_LEFT:  orientDeg = 180; break;
        case PIN_ORIENTATION::PIN_DOWN:  orientDeg = 270; break;
        case PIN_ORIENTATION::INHERIT:   orientDeg = 0; break;  // resolved by PinDrawOrient
        default: break;
    }
    resp.set_orientation_degrees( orientDeg );
    // Tip of the pin (for label/wire end); wire must start at position (body) to connect.
    resp.mutable_position_label()->set_x_mm( tipIU.x / SCH_IU_PER_MM );
    resp.mutable_position_label()->set_y_mm( tipIU.y / SCH_IU_PER_MM );
    return resp;
}


HANDLER_RESULT<GetDanglingReportResponse> API_HANDLER_SCH::handleGetDanglingReport(
        const HANDLER_CONTEXT<GetDanglingReport>& aCtx )
{
    (void) aCtx;
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No schematic open" );
        return tl::unexpected( err );
    }

    const SCH_SHEET_PATH& sheet = m_frame->GetCurrentSheet();
    screen->TestDanglingEnds( &sheet, nullptr );

    GetDanglingReportResponse resp;
    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() == SCH_SYMBOL_T )
        {
            SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );
            for( SCH_PIN* pin : sym->GetPins( &sheet ) )
            {
                if( pin->IsDangling() )
                {
                    // pin->GetPosition() returns world coordinates for schematic pins.
                    // Do NOT use GetPinPhysicalPosition — it double-transforms schematic pins.
                    VECTOR2I posIU = pin->GetPosition();
                    auto* di = resp.add_items();
                    di->set_reference( sym->GetRef( &sheet ).ToStdString() );
                    di->set_pin_number( pin->GetNumber().ToStdString() );
                    di->set_x_mm( posIU.x / SCH_IU_PER_MM );
                    di->set_y_mm( posIU.y / SCH_IU_PER_MM );
                    di->set_type( "pin" );
                }
            }
        }
        else if( item->Type() == SCH_LINE_T )
        {
            SCH_LINE* line = static_cast<SCH_LINE*>( item );
            if( line->GetLayer() != LAYER_WIRE )
                continue;
            if( line->IsStartDangling() )
            {
                VECTOR2I pos = line->GetStartPoint();
                auto* di = resp.add_items();
                di->set_x_mm( pos.x / SCH_IU_PER_MM );
                di->set_y_mm( pos.y / SCH_IU_PER_MM );
                di->set_type( "wire_end" );
            }
            if( line->IsEndDangling() )
            {
                VECTOR2I pos = line->GetEndPoint();
                auto* di = resp.add_items();
                di->set_x_mm( pos.x / SCH_IU_PER_MM );
                di->set_y_mm( pos.y / SCH_IU_PER_MM );
                di->set_type( "wire_end" );
            }
        }
    }
    return resp;
}


HANDLER_RESULT<CreateNoConnectsResponse> API_HANDLER_SCH::handleCreateNoConnects(
        const HANDLER_CONTEXT<CreateNoConnects>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const CreateNoConnects& req = aCtx.Request;

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No schematic open" );
        return tl::unexpected( err );
    }

    if( req.points_size() == 0 )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "points (x_mm, y_mm) are required" );
        return tl::unexpected( err );
    }

    bool implicitCommit = false;
    COMMIT* commit = getCommitByIdOrImplicit( req.commit_id().value(), aCtx.ClientName,
                                              implicitCommit );
    if( !commit )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "Invalid or expired commit ID" );
        return tl::unexpected( err );
    }

    // Existing marker positions: skipping them keeps the call idempotent.
    std::set<std::pair<int, int>> existing;
    for( SCH_ITEM* item : screen->Items().OfType( SCH_NO_CONNECT_T ) )
    {
        VECTOR2I pos = static_cast<SCH_NO_CONNECT*>( item )->GetPosition();
        existing.insert( { pos.x, pos.y } );
    }

    int created = 0;
    for( int i = 0; i < req.points_size(); ++i )
    {
        VECTOR2I posIU( KiROUND( req.points( i ).x_mm() * SCH_IU_PER_MM ),
                        KiROUND( req.points( i ).y_mm() * SCH_IU_PER_MM ) );
        if( !existing.insert( { posIU.x, posIU.y } ).second )
            continue;

        SCH_NO_CONNECT* nc = new SCH_NO_CONNECT( posIU );
        commit->Add( nc, screen );
        ++created;
    }

    pushImplicitCommit( implicitCommit, aCtx.ClientName, _( "Added no-connects via API" ) );

    CreateNoConnectsResponse resp;
    resp.set_created( created );
    return resp;
}


HANDLER_RESULT<GetSchematicSummaryResponse> API_HANDLER_SCH::handleGetSchematicSummary(
        const HANDLER_CONTEXT<GetSchematicSummary>& aCtx )
{
    (void) aCtx;
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No schematic open" );
        return tl::unexpected( err );
    }

    const SCH_SHEET_PATH& sheet = m_frame->GetCurrentSheet();
    GetSchematicSummaryResponse resp;
    resp.set_sheet_path( sheet.PathAsString().ToStdString() );

    // Current snapping grid (so tools use coordinates that snap correctly)
    if( EESCHEMA_SETTINGS* settings = m_frame->eeconfig() )
    {
        const GRID_SETTINGS& gridSettings = settings->m_Window.grid;
        int idx = gridSettings.last_size_idx;
        if( idx >= 0 && idx < (int) gridSettings.grids.size() )
        {
            const GRID& g = gridSettings.grids[idx];
            VECTOR2D stepMm = g.ToDouble( m_frame->GetIuScale() );
            resp.set_grid_step_mm( stepMm.x );
            resp.set_grid_display( g.UserUnitsMessageText( m_frame ).ToStdString() );
        }
    }

    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() == SCH_SYMBOL_T )
        {
            SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );
            auto* comp = resp.add_components();
            comp->set_reference( sym->GetRef( &sheet ).ToStdString() );
            comp->set_library_nickname( sym->GetLibId().GetLibNickname().c_str() );
            comp->set_symbol_name( sym->GetLibId().GetLibItemName().c_str() );
            comp->set_value( sym->GetValue( false, &sheet, false ).ToStdString() );
            comp->set_footprint( sym->GetFootprintFieldText( false, &sheet, false ).ToStdString() );
            for( const SCH_FIELD& field : sym->GetFields() )
            {
                auto* fieldOut = comp->add_fields();
                fieldOut->set_name( field.GetCanonicalName().ToStdString() );
                fieldOut->set_value( field.GetText().ToStdString() );
                fieldOut->set_visible( field.IsVisible() );
            }
            VECTOR2I posIU = sym->GetPosition();
            comp->mutable_position()->set_x_mm( posIU.x / SCH_IU_PER_MM );
            comp->mutable_position()->set_y_mm( posIU.y / SCH_IU_PER_MM );
            int orient = sym->GetOrientation() & ( SYM_ORIENT_0 | SYM_ORIENT_90 | SYM_ORIENT_180 | SYM_ORIENT_270 );
            if( orient == SYM_ORIENT_90 )
                comp->set_rotation( 90.0 );
            else if( orient == SYM_ORIENT_180 )
                comp->set_rotation( 180.0 );
            else if( orient == SYM_ORIENT_270 )
                comp->set_rotation( 270.0 );
            else
                comp->set_rotation( 0.0 );
            BOX2I bboxIU = sym->GetBodyAndPinsBoundingBox();
            comp->mutable_bbox()->set_min_x_mm( bboxIU.GetLeft() / SCH_IU_PER_MM );
            comp->mutable_bbox()->set_min_y_mm( bboxIU.GetTop() / SCH_IU_PER_MM );
            comp->mutable_bbox()->set_max_x_mm( bboxIU.GetRight() / SCH_IU_PER_MM );
            comp->mutable_bbox()->set_max_y_mm( bboxIU.GetBottom() / SCH_IU_PER_MM );
            for( SCH_PIN* pin : sym->GetPins( &sheet ) )
            {
                auto* ps = comp->add_pins();
                ps->set_number( pin->GetNumber().ToStdString() );
                ps->set_name( pin->GetName().ToStdString() );
                // pin->GetPosition() returns world coordinates for schematic pins.
                // Do NOT use GetPinPhysicalPosition — it double-transforms schematic pins.
                VECTOR2I pinPosIU = pin->GetPosition();
                ps->set_x_mm( pinPosIU.x / SCH_IU_PER_MM );
                ps->set_y_mm( pinPosIU.y / SCH_IU_PER_MM );
            }
        }
        else if( item->Type() == SCH_GLOBAL_LABEL_T )
        {
            SCH_GLOBALLABEL* label = static_cast<SCH_GLOBALLABEL*>( item );
            resp.add_global_net_names( label->GetText().ToStdString() );
        }
    }
    return resp;
}


HANDLER_RESULT<GetNetlistResponse> API_HANDLER_SCH::handleGetNetlist(
        const HANDLER_CONTEXT<GetNetlist>& aCtx )
{
    (void) aCtx;
    GetNetlistResponse resp;
    SCHEMATIC* schematic = &m_frame->Schematic();
    CONNECTION_GRAPH* graph = schematic->ConnectionGraph();
    if( !graph )
        return resp;

    const SCH_SHEET_PATH& currentSheet = m_frame->GetCurrentSheet();
    const auto& netMap = graph->GetNetMap();

    for( const auto& [key, subgraphList] : netMap )
    {
        for( CONNECTION_SUBGRAPH* subgraph : subgraphList )
        {
            if( subgraph->GetSheet() != currentSheet )
                continue;
            wxString netName = subgraph->GetNetName();
            if( netName.IsEmpty() )
                continue;
            NetEntry* entry = resp.add_nets();
            entry->set_net_name( netName.ToStdString() );
            std::set<std::pair<std::string, std::string>> seen;
            for( SCH_ITEM* item : subgraph->GetItems() )
            {
                if( item->Type() != SCH_PIN_T )
                    continue;
                SCH_PIN* pin = static_cast<SCH_PIN*>( item );
                SYMBOL* parentSym = pin->GetParentSymbol();
                SCH_SYMBOL* schSym = parentSym ? dynamic_cast<SCH_SYMBOL*>( parentSym ) : nullptr;
                if( !schSym )
                    continue;
                std::string ref = schSym->GetRef( &subgraph->GetSheet() ).ToStdString();
                std::string pinNum = pin->GetNumber().ToStdString();
                if( seen.insert( { ref, pinNum } ).second )
                {
                    NetPinRef* pref = entry->add_pins();
                    pref->set_reference( ref );
                    pref->set_pin_number( pinNum );
                }
            }
            break; // one subgraph per net on this sheet is enough
        }
    }
    return resp;
}


HANDLER_RESULT<CaptureScreenshotResponse> API_HANDLER_SCH::handleCaptureScreenshot(
        const HANDLER_CONTEXT<CaptureScreenshot>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    EDA_DRAW_PANEL_GAL* canvas = m_frame->GetCanvas();
    if( !canvas || !canvas->GetView() )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No schematic canvas" );
        return tl::unexpected( err );
    }

    const double centerXmm = aCtx.Request.center_x_mm();
    const double centerYmm = aCtx.Request.center_y_mm();
    const VECTOR2D centerIU( centerXmm * SCH_IU_PER_MM, centerYmm * SCH_IU_PER_MM );

    canvas->GetView()->SetCenter( centerIU );
    m_frame->GetScreen()->m_ScrollCenter = centerIU;
    canvas->ForceRefresh();

    wxImage image;
    KIGFX::GAL* gal = canvas->GetGAL();
    if( KIGFX::OPENGL_GAL* ogl = dynamic_cast<KIGFX::OPENGL_GAL*>( gal ) )
    {
        if( !ogl->SaveScreenshot( image ) || !image.IsOk() )
        {
            ApiResponseStatus err;
            err.set_status( ApiStatusCode::AS_BAD_REQUEST );
            err.set_error_message( "Screenshot capture failed (OpenGL)" );
            return tl::unexpected( err );
        }
    }
    else
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_UNIMPLEMENTED );
        err.set_error_message( "Screenshot only supported with OpenGL canvas" );
        return tl::unexpected( err );
    }

    wxMemoryOutputStream memStream;
    if( !image.SaveFile( memStream, wxBITMAP_TYPE_PNG ) )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "PNG encode failed" );
        return tl::unexpected( err );
    }

    size_t len = memStream.GetLength();
    wxMemoryBuffer buf( len );
    memStream.CopyTo( buf.GetData(), len );
    buf.SetDataLen( len );
    wxString base64 = wxBase64Encode( buf.GetData(), buf.GetDataLen() );

    CaptureScreenshotResponse resp;
    resp.set_image_png_base64( base64.ToStdString() );
    return resp;
}


HANDLER_RESULT<CaptureScreenshotResponse> API_HANDLER_SCH::handleCaptureZoneScreenshot(
        const HANDLER_CONTEXT<CaptureZoneScreenshot>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    EDA_DRAW_PANEL_GAL* canvas = m_frame->GetCanvas();
    if( !canvas || !canvas->GetView() )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No schematic canvas" );
        return tl::unexpected( err );
    }

    KIGFX::VIEW* view = canvas->GetView();
    EDA_DRAW_FRAME* frame = static_cast<EDA_DRAW_FRAME*>( m_frame );
    const double centerXmm = aCtx.Request.center_x_mm();
    const double centerYmm = aCtx.Request.center_y_mm();
    double widthMm = aCtx.Request.width_mm();
    if( widthMm <= 0 )
        widthMm = 15.0;

    // Use full schematic view setup (same as CaptureFullSchematic), then crop to zone
    BOX2I bBox = frame->GetDocumentExtents( true );
    BOX2I defaultBox = canvas->GetDefaultViewBBox();
    if( bBox.GetWidth() == 0 || bBox.GetHeight() == 0 )
        bBox = defaultBox;

    view->SetScale( 1.0 );
    VECTOR2D screenSize = view->ToWorld( ToVECTOR2I( canvas->GetClientSize() ), false );
    VECTOR2D vsize = bBox.GetSize();
    double scale = view->GetScale()
            / std::max( fabs( vsize.x / screenSize.x ), fabs( vsize.y / screenSize.y ) );

    if( !std::isfinite( scale ) || scale <= 0 )
        view->SetCenter( VECTOR2D( 0, 0 ) );
    else
    {
        const double margin = 1.04;
        view->SetScale( scale / margin );
        view->SetCenter( bBox.Centre() );
    }

    m_frame->GetScreen()->m_ScrollCenter = view->GetCenter();
    canvas->ForceRefresh();

    wxImage image;
    KIGFX::GAL* gal = canvas->GetGAL();
    if( KIGFX::OPENGL_GAL* ogl = dynamic_cast<KIGFX::OPENGL_GAL*>( gal ) )
    {
        if( !ogl->SaveScreenshot( image ) || !image.IsOk() )
        {
            ApiResponseStatus err;
            err.set_status( ApiStatusCode::AS_BAD_REQUEST );
            err.set_error_message( "Screenshot capture failed (OpenGL)" );
            return tl::unexpected( err );
        }
    }
    else
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_UNIMPLEMENTED );
        err.set_error_message( "Screenshot only supported with OpenGL canvas" );
        return tl::unexpected( err );
    }

    // Crop to zone: zone bbox in world IU, convert to screen pixels
    const VECTOR2D centerIU( centerXmm * SCH_IU_PER_MM, centerYmm * SCH_IU_PER_MM );
    const double halfWidthIU = ( widthMm / 2.0 ) * SCH_IU_PER_MM;
    const double aspect = ( screenSize.y > 0 ) ? ( screenSize.x / screenSize.y ) : 1.0;
    const double halfHeightIU = halfWidthIU / aspect;

    VECTOR2D corners[4] = {
        { centerIU.x - halfWidthIU, centerIU.y - halfHeightIU },
        { centerIU.x + halfWidthIU, centerIU.y - halfHeightIU },
        { centerIU.x + halfWidthIU, centerIU.y + halfHeightIU },
        { centerIU.x - halfWidthIU, centerIU.y + halfHeightIU },
    };

    double minPxX = 1e9, maxPxX = -1e9, minPxY = 1e9, maxPxY = -1e9;
    for( const VECTOR2D& corner : corners )
    {
        VECTOR2D px = view->ToScreen( corner );
        minPxX = std::min( minPxX, px.x );
        maxPxX = std::max( maxPxX, px.x );
        minPxY = std::min( minPxY, px.y );
        maxPxY = std::max( maxPxY, px.y );
    }

    int imgW = image.GetWidth();
    int imgH = image.GetHeight();
    int x0 = std::max( 0, KiROUND( minPxX ) );
    int y0 = std::max( 0, KiROUND( minPxY ) );
    int x1 = std::min( imgW, KiROUND( maxPxX ) );
    int y1 = std::min( imgH, KiROUND( maxPxY ) );
    int cropW = std::max( 1, x1 - x0 );
    int cropH = std::max( 1, y1 - y0 );

    if( cropW < imgW || cropH < imgH )
        image = image.GetSubImage( wxRect( x0, y0, cropW, cropH ) );

    int32_t maxWidthPx = aCtx.Request.max_width_px();
    if( maxWidthPx > 0 && image.GetWidth() > maxWidthPx )
    {
        int newW = maxWidthPx;
        int newH = ( image.GetHeight() * maxWidthPx ) / image.GetWidth();
        if( newH < 1 )
            newH = 1;
        image = image.Rescale( newW, newH, wxIMAGE_QUALITY_BILINEAR );
    }

    wxMemoryOutputStream memStream;
    if( !image.SaveFile( memStream, wxBITMAP_TYPE_PNG ) )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "PNG encode failed" );
        return tl::unexpected( err );
    }

    size_t len = memStream.GetLength();
    wxMemoryBuffer buf( len );
    memStream.CopyTo( buf.GetData(), len );
    buf.SetDataLen( len );
    wxString base64 = wxBase64Encode( buf.GetData(), buf.GetDataLen() );

    CaptureScreenshotResponse resp;
    resp.set_image_png_base64( base64.ToStdString() );
    return resp;
}


HANDLER_RESULT<CaptureScreenshotResponse> API_HANDLER_SCH::handleCaptureFullSchematic(
        const HANDLER_CONTEXT<CaptureFullSchematic>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    EDA_DRAW_PANEL_GAL* canvas = m_frame->GetCanvas();
    if( !canvas || !canvas->GetView() )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No schematic canvas" );
        return tl::unexpected( err );
    }

    wxSize savedSize;
    int32_t targetWidthPx = aCtx.Request.target_width_px();
    if( targetWidthPx > 0 )
    {
        savedSize = canvas->GetSize();
        int curW = savedSize.GetWidth();
        int curH = savedSize.GetHeight();
        if( curW > 0 && curH > 0 )
        {
            int newH = ( curH * targetWidthPx ) / curW;
            if( newH < 1 )
                newH = 1;
            canvas->SetSize( targetWidthPx, newH );
            if( wxTheApp )
                wxTheApp->ProcessPendingEvents();
            canvas->Refresh();
        }
    }

    KIGFX::VIEW* view = canvas->GetView();
    EDA_DRAW_FRAME* frame = static_cast<EDA_DRAW_FRAME*>( m_frame );
    BOX2I bBox = frame->GetDocumentExtents( true );
    BOX2I defaultBox = canvas->GetDefaultViewBBox();

    if( bBox.GetWidth() == 0 || bBox.GetHeight() == 0 )
        bBox = defaultBox;

    view->SetScale( 1.0 );
    VECTOR2D screenSize = view->ToWorld( ToVECTOR2I( canvas->GetClientSize() ), false );
    VECTOR2D vsize = bBox.GetSize();
    double scale = view->GetScale()
            / std::max( fabs( vsize.x / screenSize.x ), fabs( vsize.y / screenSize.y ) );

    if( !std::isfinite( scale ) || scale <= 0 )
    {
        view->SetCenter( VECTOR2D( 0, 0 ) );
    }
    else
    {
        const double margin = 1.04;
        view->SetScale( scale / margin );
        view->SetCenter( bBox.Centre() );
    }

    m_frame->GetScreen()->m_ScrollCenter = view->GetCenter();
    canvas->ForceRefresh();

    wxImage image;
    KIGFX::GAL* gal = canvas->GetGAL();
    if( KIGFX::OPENGL_GAL* ogl = dynamic_cast<KIGFX::OPENGL_GAL*>( gal ) )
    {
        if( !ogl->SaveScreenshot( image ) || !image.IsOk() )
        {
            ApiResponseStatus err;
            err.set_status( ApiStatusCode::AS_BAD_REQUEST );
            err.set_error_message( "Screenshot capture failed (OpenGL)" );
            return tl::unexpected( err );
        }
    }
    else
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_UNIMPLEMENTED );
        err.set_error_message( "Screenshot only supported with OpenGL canvas" );
        return tl::unexpected( err );
    }

    if( targetWidthPx > 0 && savedSize.GetWidth() > 0 && savedSize.GetHeight() > 0 )
    {
        canvas->SetSize( savedSize );
        if( wxTheApp )
            wxTheApp->ProcessPendingEvents();
    }

    wxMemoryOutputStream memStream;
    if( !image.SaveFile( memStream, wxBITMAP_TYPE_PNG ) )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "PNG encode failed" );
        return tl::unexpected( err );
    }

    size_t len = memStream.GetLength();
    wxMemoryBuffer buf( len );
    memStream.CopyTo( buf.GetData(), len );
    buf.SetDataLen( len );
    wxString base64 = wxBase64Encode( buf.GetData(), buf.GetDataLen() );

    CaptureScreenshotResponse resp;
    resp.set_image_png_base64( base64.ToStdString() );
    return resp;
}


HANDLER_RESULT<GetVisibleBoundsResponse> API_HANDLER_SCH::handleGetVisibleBounds(
        const HANDLER_CONTEXT<GetVisibleBounds>& aCtx )
{
    (void) aCtx;

    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    EDA_DRAW_PANEL_GAL* canvas = m_frame->GetCanvas();
    if( !canvas || !canvas->GetView() )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No schematic canvas" );
        return tl::unexpected( err );
    }

    KIGFX::VIEW* view = canvas->GetView();
    const wxSize clientSize = canvas->GetClientSize();
    const VECTOR2D worldSpan = view->ToWorld( ToVECTOR2I( clientSize ), false );
    const VECTOR2D centerIU = view->GetCenter();

    const double halfWidthIu = worldSpan.x / 2.0;
    const double halfHeightIu = worldSpan.y / 2.0;

    GetVisibleBoundsResponse resp;
    resp.set_min_x_mm( ( centerIU.x - halfWidthIu ) / SCH_IU_PER_MM );
    resp.set_min_y_mm( ( centerIU.y - halfHeightIu ) / SCH_IU_PER_MM );
    resp.set_max_x_mm( ( centerIU.x + halfWidthIu ) / SCH_IU_PER_MM );
    resp.set_max_y_mm( ( centerIU.y + halfHeightIu ) / SCH_IU_PER_MM );
    resp.set_center_x_mm( centerIU.x / SCH_IU_PER_MM );
    resp.set_center_y_mm( centerIU.y / SCH_IU_PER_MM );
    resp.set_width_mm( worldSpan.x / SCH_IU_PER_MM );
    resp.set_height_mm( worldSpan.y / SCH_IU_PER_MM );
    resp.set_client_width_px( clientSize.GetWidth() );
    resp.set_client_height_px( clientSize.GetHeight() );
    return resp;
}


HANDLER_RESULT<MoveComponentResponse> API_HANDLER_SCH::handleMoveComponent(
        const HANDLER_CONTEXT<MoveComponent>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const MoveComponent& req = aCtx.Request;
    if( req.reference().empty() || !req.has_position() )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "reference and position (x_mm, y_mm) are required" );
        return tl::unexpected( err );
    }

    std::string commitIdStr = req.commit_id().value();
    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No schematic open" );
        return tl::unexpected( err );
    }

    const SCH_SHEET_PATH& sheet = m_frame->GetCurrentSheet();
    wxString refReq( req.reference().c_str(), wxConvUTF8 );
    SCH_SYMBOL* symbol = nullptr;
    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() != SCH_SYMBOL_T )
            continue;
        SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );
        if( sym->GetRef( &sheet ) == refReq )
        {
            symbol = sym;
            break;
        }
    }
    if( !symbol )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "Symbol not found: " + req.reference() );
        return tl::unexpected( err );
    }

    bool implicitCommit = false;
    COMMIT* commit = getCommitByIdOrImplicit( commitIdStr, aCtx.ClientName, implicitCommit );
    if( !commit )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "Invalid or expired commit ID" );
        return tl::unexpected( err );
    }

    double xMm = req.position().x_mm();
    double yMm = req.position().y_mm();
    VECTOR2I newPosIU( KiROUND( xMm * SCH_IU_PER_MM ), KiROUND( yMm * SCH_IU_PER_MM ) );
    VECTOR2I oldPosIU = symbol->GetPosition();
    VECTOR2I delta = newPosIU - oldPosIU;
    symbol->Move( delta );

    if( req.has_rotation() )
    {
        int targetOrient = SYM_ORIENT_0;
        double rot = req.rotation();
        if( rot >= 45 && rot < 135 )
            targetOrient = SYM_ORIENT_90;
        else if( rot >= 135 && rot < 225 )
            targetOrient = SYM_ORIENT_180;
        else if( rot >= 225 && rot < 315 )
            targetOrient = SYM_ORIENT_270;
        int curOrient = symbol->GetOrientation() & ( SYM_ORIENT_0 | SYM_ORIENT_90 | SYM_ORIENT_180 | SYM_ORIENT_270 );
        int curDeg = ( curOrient == SYM_ORIENT_90 ) ? 90 : ( curOrient == SYM_ORIENT_180 ) ? 180 : ( curOrient == SYM_ORIENT_270 ) ? 270 : 0;
        int targetDeg = ( targetOrient == SYM_ORIENT_90 ) ? 90 : ( targetOrient == SYM_ORIENT_180 ) ? 180 : ( targetOrient == SYM_ORIENT_270 ) ? 270 : 0;
        int steps = ( ( targetDeg - curDeg + 360 ) % 360 ) / 90;
        for( int i = 0; i < steps; ++i )
            symbol->Rotate( symbol->GetPosition(), true );
    }

    commit->Modify( symbol, screen );
    pushImplicitCommit( implicitCommit, aCtx.ClientName, _( "Moved component via API" ) );
    return MoveComponentResponse();
}


HANDLER_RESULT<SetComponentFootprintResponse> API_HANDLER_SCH::handleSetComponentFootprint(
        const HANDLER_CONTEXT<SetComponentFootprint>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const SetComponentFootprint& req = aCtx.Request;
    if( req.reference().empty() || req.footprint().empty() )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "reference and footprint are required" );
        return tl::unexpected( err );
    }

    std::string commitIdStr = req.commit_id().value();
    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No schematic open" );
        return tl::unexpected( err );
    }

    const SCH_SHEET_PATH& sheet = m_frame->GetCurrentSheet();
    wxString refReq( req.reference().c_str(), wxConvUTF8 );
    SCH_SYMBOL* symbol = nullptr;
    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() != SCH_SYMBOL_T )
            continue;

        SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );
        if( sym->GetRef( &sheet ) == refReq )
        {
            symbol = sym;
            break;
        }
    }
    if( !symbol )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "Symbol not found: " + req.reference() );
        return tl::unexpected( err );
    }

    bool implicitCommit = false;
    COMMIT* commit = getCommitByIdOrImplicit( commitIdStr, aCtx.ClientName, implicitCommit );
    if( !commit )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "Invalid or expired commit ID" );
        return tl::unexpected( err );
    }

    wxString oldFp = symbol->GetFootprintFieldText( false, &sheet, false );
    wxString newFp( req.footprint().c_str(), wxConvUTF8 );
    symbol->SetFootprintFieldText( newFp );
    commit->Modify( symbol, screen );
    pushImplicitCommit( implicitCommit, aCtx.ClientName, _( "Updated component footprint via API" ) );

    SetComponentFootprintResponse resp;
    resp.set_reference( req.reference() );
    resp.set_old_footprint( oldFp.ToStdString() );
    resp.set_new_footprint( req.footprint() );
    return resp;
}


HANDLER_RESULT<SetComponentFieldsResponse> API_HANDLER_SCH::handleSetComponentFields(
        const HANDLER_CONTEXT<SetComponentFields>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const SetComponentFields& req = aCtx.Request;
    if( req.reference().empty() || req.fields().empty() )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "reference and at least one field are required" );
        return tl::unexpected( err );
    }

    std::string commitIdStr = req.commit_id().value();
    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No schematic open" );
        return tl::unexpected( err );
    }

    const SCH_SHEET_PATH& sheet = m_frame->GetCurrentSheet();
    wxString refReq( req.reference().c_str(), wxConvUTF8 );
    SCH_SYMBOL* symbol = nullptr;
    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() != SCH_SYMBOL_T )
            continue;

        SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );
        if( sym->GetRef( &sheet ) == refReq )
        {
            symbol = sym;
            break;
        }
    }
    if( !symbol )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "Symbol not found: " + req.reference() );
        return tl::unexpected( err );
    }

    bool implicitCommit = false;
    COMMIT* commit = getCommitByIdOrImplicit( commitIdStr, aCtx.ClientName, implicitCommit );
    if( !commit )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "Invalid or expired commit ID" );
        return tl::unexpected( err );
    }

    SetComponentFieldsResponse resp;
    resp.set_reference( req.reference() );

    for( const auto& reqField : req.fields() )
    {
        if( reqField.name().empty() )
            continue;

        wxString fieldName( reqField.name().c_str(), wxConvUTF8 );
        wxString fieldValue( reqField.value().c_str(), wxConvUTF8 );
        SCH_FIELD* field = symbol->GetField( fieldName );
        if( !field )
        {
            SCH_FIELD newField( symbol, FIELD_T::USER, fieldName );
            field = symbol->AddField( newField );
            field->SetVisible( reqField.visible() );
        }

        field->SetText( fieldValue );
        field->SetVisible( reqField.visible() );

        auto* outField = resp.add_fields();
        outField->set_name( reqField.name() );
        outField->set_value( reqField.value() );
        outField->set_visible( reqField.visible() );
    }

    commit->Modify( symbol, screen );
    pushImplicitCommit( implicitCommit, aCtx.ClientName, _( "Updated component fields via API" ) );
    return resp;
}


HANDLER_RESULT<DeleteComponentResponse> API_HANDLER_SCH::handleDeleteComponent(
        const HANDLER_CONTEXT<DeleteComponent>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const DeleteComponent& req = aCtx.Request;
    if( req.reference().empty() )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "reference is required" );
        return tl::unexpected( err );
    }

    std::string commitIdStr = req.commit_id().value();
    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "No schematic open" );
        return tl::unexpected( err );
    }

    const SCH_SHEET_PATH& sheet = m_frame->GetCurrentSheet();
    wxString refReq( req.reference().c_str(), wxConvUTF8 );
    SCH_SYMBOL* symbol = nullptr;
    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->Type() != SCH_SYMBOL_T )
            continue;
        SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );
        if( sym->GetRef( &sheet ) == refReq )
        {
            symbol = sym;
            break;
        }
    }
    if( !symbol )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "Symbol not found: " + req.reference() );
        return tl::unexpected( err );
    }

    bool implicitCommit = false;
    COMMIT* commit = getCommitByIdOrImplicit( commitIdStr, aCtx.ClientName, implicitCommit );
    if( !commit )
    {
        ApiResponseStatus err;
        err.set_status( ApiStatusCode::AS_BAD_REQUEST );
        err.set_error_message( "Invalid or expired commit ID" );
        return tl::unexpected( err );
    }

    commit->Remove( symbol, screen );
    pushImplicitCommit( implicitCommit, aCtx.ClientName, _( "Deleted component via API" ) );
    return DeleteComponentResponse();
}


HANDLER_RESULT<ReloadProjectSymbolLibrariesResponse>
API_HANDLER_SCH::handleReloadProjectSymbolLibraries(
        const HANDLER_CONTEXT<ReloadProjectSymbolLibraries>& aCtx )
{
    ReloadProjectSymbolLibrariesResponse response;

    // Reload the PROJECT symbol libraries from disk AND invalidate the per-project library
    // adapter caches, so libraries imported mid-session (e.g. Copper_<id> written by
    // import_lcsc_part) become resolvable to the placer. The original GlobalTablesChanged()
    // only cleared the GLOBAL cache; reloading the table alone is not enough either, because
    // place_component resolves through SymbolLibAdapter whose cache must also be cleared.
    // ProjectChanged() does both: LoadProjectTables() + adapter->ProjectChanged() for every
    // registered adapter.
    Pgm().GetLibraryManager().ProjectChanged();

    // Load project rows immediately so API placement can resolve newly added
    // libraries without waiting for the async preload path used at project open.
    if( SYMBOL_LIBRARY_ADAPTER* adapter = PROJECT_SCH::SymbolLibAdapter( &m_frame->Prj() ) )
    {
        for( LIBRARY_TABLE_ROW* row : adapter->Rows( LIBRARY_TABLE_SCOPE::PROJECT ) )
            adapter->LoadOne( row->Nickname() );
    }

    broadcastSymbolLibraryReload( m_frame );
    response.set_ok( true );
    return response;
}


HANDLER_RESULT<AppendProjectSymbolLibraryRowResponse>
API_HANDLER_SCH::handleAppendProjectSymbolLibraryRow(
        const HANDLER_CONTEXT<AppendProjectSymbolLibraryRow>& aCtx )
{
    AppendProjectSymbolLibraryRowResponse response;
    const AppendProjectSymbolLibraryRow&  req = aCtx.Request;

    wxString nick = wxString::FromUTF8( req.library_nickname() );
    wxString uri = wxString::FromUTF8( req.uri() );
    wxString descr = wxString::FromUTF8( req.description() );

    if( nick.IsEmpty() || uri.IsEmpty() )
    {
        response.set_ok( false );
        response.set_error_message( "library_nickname and uri are required" );
        return response;
    }

    PROJECT&                prj = m_frame->Prj();
    SYMBOL_LIBRARY_ADAPTER* adapter = PROJECT_SCH::SymbolLibAdapter( &prj );

    const bool replace = req.replace_existing();

    if( adapter->HasLibrary( nick, false ) && !replace )
    {
        response.set_ok( true );
        response.set_skipped_duplicate( true );
        response.set_wrote_file( false );
        return response;
    }

    auto optTbl = adapter->ProjectTable();
    if( !optTbl )
    {
        response.set_ok( false );
        response.set_error_message( "No project symbol library table" );
        return response;
    }
    LIBRARY_TABLE_ROW& row = (*optTbl)->InsertRow();
    row.SetNickname( nick );
    row.SetURI( uri );
    row.SetType( wxT( "KiCad" ) );
    row.SetDescription( descr );

    auto saveResult = (*optTbl)->Save();
    if( !saveResult )
    {
        response.set_ok( false );
        response.set_error_message( saveResult.error().message.ToUTF8() );
        return response;
    }

    // Refresh PROJECT-scoped caches only — see handleWriteSymbolLibraryFile:
    // GlobalTablesChanged() wipes the GLOBAL cache and trips a latent SCH_PIN
    // double-free on teardown. A ${KIPRJMOD} project library doesn't need it.
    Pgm().GetLibraryManager().ProjectChanged();
    for( LIBRARY_TABLE_ROW* libRow : adapter->Rows( LIBRARY_TABLE_SCOPE::PROJECT ) )
        adapter->LoadOne( libRow->Nickname() );
    broadcastSymbolLibraryReload( m_frame );

    response.set_ok( true );
    response.set_skipped_duplicate( false );
    response.set_wrote_file( true );
    return response;
}


HANDLER_RESULT<WriteSymbolLibraryFileResponse>
API_HANDLER_SCH::handleWriteSymbolLibraryFile(
        const HANDLER_CONTEXT<WriteSymbolLibraryFile>& aCtx )
{
    WriteSymbolLibraryFileResponse response;
    const WriteSymbolLibraryFile&  req = aCtx.Request;

    // Validate required fields
    if( req.relative_path().empty() )
    {
        response.set_ok( false );
        response.set_error_message( "relative_path is required" );
        return response;
    }
    if( req.library_nickname().empty() )
    {
        response.set_ok( false );
        response.set_error_message( "library_nickname is required" );
        return response;
    }

    // Reject path traversal
    const std::string& relPath = req.relative_path();
    if( relPath.find( ".." ) != std::string::npos )
    {
        response.set_ok( false );
        response.set_error_message( "relative_path must not contain '..' (path traversal rejected)" );
        return response;
    }

    // Resolve project path
    PROJECT& prj = m_frame->Prj();
    wxString projectPath = prj.GetProjectPath();
    if( projectPath.IsEmpty() )
    {
        response.set_ok( false );
        response.set_error_message( "No project is open (KIPRJMOD is empty)" );
        return response;
    }

    // Build absolute output path by appending the relative path to the project root
    wxFileName relFn( wxString::FromUTF8( relPath ) );
    wxFileName outFn( projectPath );
    // Append each directory component
    wxArrayString dirs = relFn.GetDirs();
    for( const wxString& d : dirs )
        outFn.AppendDir( d );
    outFn.SetFullName( relFn.GetFullName() );
    outFn.Normalize( wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE );

    const wxString absPath = outFn.GetFullPath();

    // Safety check: resolved path must be inside the project directory
    if( !absPath.StartsWith( projectPath ) )
    {
        response.set_ok( false );
        response.set_error_message( "resolved path escapes the project directory" );
        return response;
    }

    // Create parent directories
    wxFileName parentFn( absPath );
    parentFn.SetFullName( wxEmptyString );
    if( !parentFn.Mkdir( 0755, wxPATH_MKDIR_FULL ) && !parentFn.DirExists() )
    {
        response.set_ok( false );
        response.set_error_message( "Failed to create parent directories for: " + absPath.ToStdString() );
        return response;
    }

    // Write file content
    wxFile outFile;
    if( !outFile.Open( absPath, wxFile::write ) )
    {
        response.set_ok( false );
        response.set_error_message( "Cannot open file for writing: " + absPath.ToStdString() );
        return response;
    }

    const std::string& content = req.content();
    if( !content.empty() )
    {
        if( outFile.Write( content.data(), content.size() ) != content.size() )
        {
            outFile.Close();
            response.set_ok( false );
            response.set_error_message( "Failed to write all bytes to: " + absPath.ToStdString() );
            return response;
        }
    }
    outFile.Close();

    response.set_resolved_path( absPath.ToUTF8() );

    // Register the library in sym-lib-table (same logic as handleAppendProjectSymbolLibraryRow)
    SYMBOL_LIBRARY_ADAPTER* adapter = PROJECT_SCH::SymbolLibAdapter( &prj );
    wxString nick = wxString::FromUTF8( req.library_nickname() );
    wxString descr = wxString::FromUTF8( req.description() );

    // Build ${KIPRJMOD}-relative URI; normalise to forward slashes
    wxString relForUri = wxString::FromUTF8( relPath );
    relForUri.Replace( wxT( "\\" ), wxT( "/" ) );
    wxString uri = wxString( "${KIPRJMOD}/" ) + relForUri;

    const bool replace = req.replace_existing();

    if( !adapter->HasLibrary( nick, false ) || replace )
    {
        auto optTbl = adapter->ProjectTable();
        if( !optTbl )
        {
            response.set_ok( false );
            response.set_error_message( "No project symbol library table" );
            return response;
        }
        LIBRARY_TABLE_ROW& row = (*optTbl)->InsertRow();
        row.SetNickname( nick );
        row.SetURI( uri );
        row.SetType( wxT( "KiCad" ) );
        row.SetDescription( descr );

        auto saveResult = (*optTbl)->Save();
        if( !saveResult )
        {
            response.set_ok( false );
            response.set_error_message( "File written but sym-lib-table save failed: "
                                        + saveResult.error().message.ToStdString() );
            return response;
        }

        // Refresh PROJECT-scoped caches only. Do NOT call GlobalTablesChanged()
        // here: clearing the whole GLOBAL symbol cache forces teardown of every
        // cached SEXPR IO plugin, which trips a latent SCH_PIN double-free (heap
        // abort, seen on the shipped DMG) in some cached LIB_SYMBOLs. A
        // ${KIPRJMOD}-relative project library never needs the global cache wiped.
        // Mirror handleReloadProjectSymbolLibraries: ProjectChanged() + eager
        // LoadOne() so the new library resolves immediately for API placement.
        Pgm().GetLibraryManager().ProjectChanged();
        for( LIBRARY_TABLE_ROW* libRow : adapter->Rows( LIBRARY_TABLE_SCOPE::PROJECT ) )
            adapter->LoadOne( libRow->Nickname() );
        broadcastSymbolLibraryReload( m_frame );
    }
    else
    {
        // Nickname already registered and replace_existing not set: the table row is
        // left untouched. Report where it points so callers can detect a nickname
        // collision with a different import location (the written file would be
        // invisible to the running session).
        response.set_skipped_existing_row( true );
        auto existingUri = Pgm().GetLibraryManager().GetFullURI(
                LIBRARY_TABLE_TYPE::SYMBOL, nick, false );
        if( existingUri )
            response.set_existing_uri( existingUri->ToUTF8() );
    }

    response.set_ok( true );
    return response;
}


HANDLER_RESULT<WriteFootprintLibraryFileResponse>
API_HANDLER_SCH::handleWriteFootprintLibraryFile(
        const HANDLER_CONTEXT<WriteFootprintLibraryFile>& aCtx )
{
    WriteFootprintLibraryFileResponse response;
    const WriteFootprintLibraryFile&  req = aCtx.Request;

    // Validate required fields
    if( req.relative_path().empty() )
    {
        response.set_ok( false );
        response.set_error_message( "relative_path is required" );
        return response;
    }
    if( req.library_nickname().empty() )
    {
        response.set_ok( false );
        response.set_error_message( "library_nickname is required" );
        return response;
    }

    // Reject path traversal
    const std::string& relPath = req.relative_path();
    if( relPath.find( ".." ) != std::string::npos )
    {
        response.set_ok( false );
        response.set_error_message( "relative_path must not contain '..' (path traversal rejected)" );
        return response;
    }

    // Resolve project path
    PROJECT& prj = m_frame->Prj();
    wxString projectPath = prj.GetProjectPath();
    if( projectPath.IsEmpty() )
    {
        response.set_ok( false );
        response.set_error_message( "No project is open (KIPRJMOD is empty)" );
        return response;
    }

    // Build absolute output path by appending the relative path to the project root
    wxFileName relFn( wxString::FromUTF8( relPath ) );
    wxFileName outFn( projectPath );
    wxArrayString dirs = relFn.GetDirs();
    for( const wxString& d : dirs )
        outFn.AppendDir( d );
    outFn.SetFullName( relFn.GetFullName() );
    outFn.Normalize( wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE );

    const wxString absPath = outFn.GetFullPath();

    // Safety check: resolved path must be inside the project directory
    if( !absPath.StartsWith( projectPath ) )
    {
        response.set_ok( false );
        response.set_error_message( "resolved path escapes the project directory" );
        return response;
    }

    // Create parent directories
    wxFileName parentFn( absPath );
    parentFn.SetFullName( wxEmptyString );
    if( !parentFn.Mkdir( 0755, wxPATH_MKDIR_FULL ) && !parentFn.DirExists() )
    {
        response.set_ok( false );
        response.set_error_message( "Failed to create parent directories for: " + absPath.ToStdString() );
        return response;
    }

    // Write file content
    wxFile outFile;
    if( !outFile.Open( absPath, wxFile::write ) )
    {
        response.set_ok( false );
        response.set_error_message( "Cannot open file for writing: " + absPath.ToStdString() );
        return response;
    }

    const std::string& content = req.content();
    if( !content.empty() )
    {
        if( outFile.Write( content.data(), content.size() ) != content.size() )
        {
            outFile.Close();
            response.set_ok( false );
            response.set_error_message( "Failed to write all bytes to: " + absPath.ToStdString() );
            return response;
        }
    }
    outFile.Close();

    response.set_resolved_path( absPath.ToUTF8() );

    // File-only writes (e.g. .3dshapes 3D model files) skip the fp-lib-table edit.
    if( req.skip_table_registration() )
    {
        response.set_ok( true );
        return response;
    }

    // Register the parent directory (the .pretty library) in the project
    // fp-lib-table file. FP_LIB_TABLE lives in pcbcommon, which the schematic
    // kiface does not link, so the table file is edited directly using the same
    // single-line row format KiCad writes. pcbnew reads the file when the board
    // opens or Update PCB from Schematic runs, so no board session is required.
    wxString nick = wxString::FromUTF8( req.library_nickname() );
    wxString descr = wxString::FromUTF8( req.description() );

    // The library URI is the parent directory of the written file, ${KIPRJMOD}-relative
    wxString relForUri = wxString::FromUTF8( relPath );
    relForUri.Replace( wxT( "\\" ), wxT( "/" ) );
    wxString libDirRel = relForUri.BeforeLast( '/' );
    wxString uri = libDirRel.IsEmpty() ? wxString( "${KIPRJMOD}" )
                                       : wxString( "${KIPRJMOD}/" ) + libDirRel;

    const bool replace = req.replace_existing();

    wxFileName fpTableFn( projectPath, wxT( "fp-lib-table" ) );
    const wxString fpTablePath = fpTableFn.GetFullPath();

    std::string table = "(fp_lib_table\n  (version 7)\n)\n";

    if( wxFileName::FileExists( fpTablePath ) )
    {
        wxFile tableIn;
        wxString existing;
        if( tableIn.Open( fpTablePath ) && tableIn.ReadAll( &existing ) )
            table = std::string( existing.ToUTF8() );
        if( tableIn.IsOpened() )
            tableIn.Close();
    }

    auto escapeTableValue = []( const wxString& value )
    {
        std::string out( value.ToUTF8() );
        size_t pos = 0;
        while( ( pos = out.find( '\\', pos ) ) != std::string::npos )
        {
            out.replace( pos, 1, "\\\\" );
            pos += 2;
        }
        pos = 0;
        while( ( pos = out.find( '"', pos ) ) != std::string::npos )
        {
            out.replace( pos, 1, "\\\"" );
            pos += 2;
        }
        return out;
    };

    // Regex-escape the nickname for the duplicate check
    std::string nickUtf8( nick.ToUTF8() );
    std::string escapedNick;
    for( char c : nickUtf8 )
    {
        switch( c )
        {
        case '.': case '*': case '+': case '?': case '^': case '$':
        case '{': case '}': case '(': case ')': case '[': case ']':
        case '|': case '\\':
            escapedNick.push_back( '\\' );
            escapedNick.push_back( c );
            break;
        default:
            escapedNick.push_back( c );
        }
    }

    bool rowExists = false;

    try
    {
        std::regex rowRe( std::string( R"(\(lib\s*\(name\s*")" ) + escapedNick + R"("\))",
                          std::regex_constants::icase );
        std::smatch m;

        if( std::regex_search( table, m, rowRe ) )
        {
            if( !replace )
            {
                rowExists = true;
                response.set_skipped_existing_row( true );

                // Extract the existing row's URI so callers can detect a nickname
                // collision with a different import location.
                size_t rowPos = static_cast<size_t>( m.position( 0 ) );
                size_t lineStart = table.rfind( '\n', rowPos );
                lineStart = ( lineStart == std::string::npos ) ? 0 : lineStart + 1;
                size_t lineEnd = table.find( '\n', rowPos );
                std::string line = table.substr(
                        lineStart, ( lineEnd == std::string::npos ? table.size() : lineEnd ) - lineStart );
                std::smatch uriMatch;
                std::regex  uriRe( R"rx(\(uri\s*"([^"]*)"\))rx" );
                if( std::regex_search( line, uriMatch, uriRe ) )
                    response.set_existing_uri( uriMatch[1].str() );
            }
            else
            {
                // Remove the existing single-line row so the new one replaces it
                size_t rowPos = static_cast<size_t>( m.position( 0 ) );
                size_t lineStart = table.rfind( '\n', rowPos );
                lineStart = ( lineStart == std::string::npos ) ? 0 : lineStart + 1;
                size_t lineEnd = table.find( '\n', rowPos );
                if( lineEnd == std::string::npos )
                    table.erase( lineStart );
                else
                    table.erase( lineStart, lineEnd - lineStart + 1 );
            }
        }
    }
    catch( const std::regex_error& )
    {
        response.set_ok( false );
        response.set_error_message( "File written but fp-lib-table duplicate check failed (bad nickname?)" );
        return response;
    }

    if( !rowExists )
    {
        size_t closeIdx = table.find_last_of( ')' );
        if( closeIdx == std::string::npos )
        {
            response.set_ok( false );
            response.set_error_message( "File written but project fp-lib-table is malformed (no closing parenthesis)" );
            return response;
        }

        std::string entry = "  (lib (name \"" + escapeTableValue( nick )
                            + "\")(type \"KiCad\")(uri \"" + escapeTableValue( uri )
                            + "\")(options \"\")(descr \"" + escapeTableValue( descr )
                            + "\"))\n";

        std::string updated = table.substr( 0, closeIdx ) + entry + ")\n";

        wxFile tableOut;
        if( !tableOut.Open( fpTablePath, wxFile::write ) )
        {
            response.set_ok( false );
            response.set_error_message( "File written but cannot open fp-lib-table for writing: "
                                        + std::string( fpTablePath.ToUTF8() ) );
            return response;
        }
        if( tableOut.Write( updated.data(), updated.size() ) != updated.size() )
        {
            tableOut.Close();
            response.set_ok( false );
            response.set_error_message( "File written but fp-lib-table write was incomplete" );
            return response;
        }
        tableOut.Close();

    }

    response.set_ok( true );
    return response;
}


HANDLER_RESULT<GetItemsResponse> API_HANDLER_SCH::handleGetItems(
        const HANDLER_CONTEXT<GetItems>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    SCH_SCREEN* screen = m_frame->GetScreen();
    if( !screen )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No schematic open" );
        return tl::unexpected( e );
    }

    GetItemsResponse response;
    bool handledAnything = false;

    for( int typeRaw : aCtx.Request.types() )
    {
        auto typeMessage = static_cast<kiapi::common::types::KiCadObjectType>( typeRaw );
        KICAD_T type = FromProtoEnum<KICAD_T>( typeMessage );

        if( type == TYPE_NOT_INIT )
            continue;

        if( type == SCH_LINE_T )
        {
            handledAnything = true;
            for( SCH_ITEM* item : screen->Items().OfType( SCH_LINE_T ) )
            {
                google::protobuf::Any itemBuf;
                item->Serialize( itemBuf );
                response.mutable_items()->Add( std::move( itemBuf ) );
            }
        }
        else if( type == SCH_LABEL_T || type == SCH_GLOBAL_LABEL_T
                 || type == SCH_HIER_LABEL_T || type == SCH_DIRECTIVE_LABEL_T )
        {
            handledAnything = true;
            for( SCH_ITEM* item : screen->Items().OfType( type ) )
            {
                google::protobuf::Any itemBuf;
                item->Serialize( itemBuf );
                response.mutable_items()->Add( std::move( itemBuf ) );
            }
        }
        else if( type == SCH_SYMBOL_T )
        {
            handledAnything = true;
            for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
            {
                google::protobuf::Any itemBuf;
                item->Serialize( itemBuf );
                response.mutable_items()->Add( std::move( itemBuf ) );
            }
        }
    }

    if( !handledAnything )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "none of the requested types are handled for a Schematic object" );
        return tl::unexpected( e );
    }

    response.set_status( kiapi::common::types::ItemRequestStatus::IRS_OK );
    return response;
}


HANDLER_RESULT<SavedDocumentResponse> API_HANDLER_SCH::handleSaveDocumentToString(
        const HANDLER_CONTEXT<SaveDocumentToString>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    SCHEMATIC* schematic = m_frame->Schematic().IsValid() ? &m_frame->Schematic() : nullptr;
    if( !schematic )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "No schematic open" );
        return tl::unexpected( e );
    }

    // Serialize the sheet currently displayed in eeschema, not always the root.
    // GetItems/DeleteItems/AddComponent and the layout engine all operate on the
    // displayed sheet, so the live text export must match: the adapter navigates
    // to (and verifies) the target sheet before requesting this export. For the
    // root sheet GetCurrentSheet().Last() == &Root(), so this preserves the
    // previous behavior; for a child sheet it exports that sub-sheet's screen,
    // which the disk-free swarm topology otherwise cannot read. Fall back to the
    // root if no current sheet is active.
    SCH_SHEET* sheetToSave = m_frame->GetCurrentSheet().Last();
    if( !sheetToSave || !sheetToSave->GetScreen() )
        sheetToSave = &schematic->Root();

    // Write to a temp file, then read back (avoids needing access to m_out internals).
    wxString tmpPath = wxFileName::CreateTempFileName( wxT( "kicad_mcp_sch_" ) );
    wxRemoveFile( tmpPath );
    wxFileName tmpFn( tmpPath );
    tmpFn.SetExt( wxT( "kicad_sch" ) );
    tmpPath = tmpFn.GetFullPath();

    try
    {
        SCH_IO_KICAD_SEXPR writer;
        writer.SaveSchematicFile( tmpPath, sheetToSave, schematic, nullptr );

        wxFile f( tmpPath );
        wxString contents;
        if( !f.IsOpened() || !f.ReadAll( &contents ) )
            THROW_IO_ERROR( "Could not read temporary schematic export" );
        f.Close();
        wxRemoveFile( tmpPath );

        SavedDocumentResponse response;
        response.mutable_document()->set_type( kiapi::common::types::DOCTYPE_SCHEMATIC );
        response.set_contents( contents.ToStdString() );
        return response;
    }
    catch( const IO_ERROR& ioe )
    {
        wxRemoveFile( tmpPath );
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( ioe.What().ToUTF8() );
        return tl::unexpected( e );
    }
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleSaveDocument(
        const HANDLER_CONTEXT<SaveDocument>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( m_frame->Prj().IsNullProject() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "schematic has no project; save it once via File > Save before "
                             "saving through the API" );
        return tl::unexpected( e );
    }

    if( !m_frame->SaveProject() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "failed to save schematic '{}'",
                                          m_frame->Schematic().GetFileName().ToStdString() ) );
        return tl::unexpected( e );
    }

    return Empty();
}


HANDLER_RESULT<Empty> API_HANDLER_SCH::handleRevertDocument(
        const HANDLER_CONTEXT<RevertDocument>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !m_commits.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BUSY );
        e.set_error_message( "cannot revert the schematic while a commit is in progress; "
                             "end the commit first" );
        return tl::unexpected( e );
    }

    SCHEMATIC& schematic = m_frame->Schematic();
    SCH_SHEET& root = schematic.Root();

    if( !wxFileName::IsFileReadable( schematic.GetFileName() ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "schematic has never been saved; nothing on disk to revert to" );
        return tl::unexpected( e );
    }

    // Reset to root first so the reverted file does not need to contain the
    // previously-current sheet (mirrors SCH_EDITOR_CONTROL::Revert).
    if( m_frame->GetCurrentSheet().Last() != &root )
    {
        m_frame->GetToolManager()->RunAction( ACTIONS::cancelInteractive );
        m_frame->GetToolManager()->RunAction( ACTIONS::selectionClear );

        SCH_SHEET_PATH rootSheetPath;
        rootSheetPath.push_back( &root );
        m_frame->SetCurrentSheet( rootSheetPath );
        m_frame->DisplayCurrentSheet();
    }

    SCH_SCREENS screenList( root );
    for( SCH_SCREEN* screen = screenList.GetFirst(); screen; screen = screenList.GetNext() )
        screen->SetContentModified( false );

    m_frame->ReleaseFile();

    wxString fileName = schematic.GetFileName();
    if( !m_frame->OpenProjectFiles( std::vector<wxString>( 1, fileName ), KICTL_REVERT ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "failed to revert schematic '{}'",
                                          fileName.ToStdString() ) );
        return tl::unexpected( e );
    }

    return Empty();
}


HANDLER_RESULT<NavigateToSheetResponse> API_HANDLER_SCH::handleNavigateToSheet(
        const HANDLER_CONTEXT<NavigateToSheet>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !m_commits.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BUSY );
        e.set_error_message( "cannot navigate sheets while a commit is in progress; "
                             "end the commit first" );
        return tl::unexpected( e );
    }

    auto humanPath =
            []( const SCH_SHEET_PATH& aPath ) -> wxString
            {
                if( aPath.size() == 1 )
                    return wxS( "/" );
                return aPath.PathHumanReadable( true, true );
            };

    wxString requested = wxString::FromUTF8( aCtx.Request.sheet_path() );

    // PathAsString() ends non-root paths with a trailing slash; accept "/<uuid>" too.
    wxString requestedKiid = requested;
    if( !requestedKiid.IsEmpty() && !requestedKiid.EndsWith( wxS( "/" ) ) )
        requestedKiid << wxS( "/" );

    // Also accept the ROOT-PREFIXED id-path form "/<rootUuid>/<uuid>/..." that the
    // adapter's disk-parsed hierarchy inventory exposes (sheetIdPath includes the
    // root sheet UUID; PathAsString() excludes it). Agents echo that form back and
    // on the no-fs live deployment the adapter forwards it verbatim — rejecting it
    // caused spurious "not found" failures (and destructive reload-retries)
    // (production Langfuse traces, 2026-07-01). UUIDs compare case-insensitively.
    wxString requestedRootStripped;
    {
        const wxString rootPrefix =
                wxS( "/" ) + m_frame->Schematic().Root().m_Uuid.AsString() + wxS( "/" );
        if( requestedKiid.Lower().StartsWith( rootPrefix.Lower() ) )
        {
            requestedRootStripped = wxS( "/" ) + requestedKiid.Mid( rootPrefix.length() );
            // "/<rootUuid>/" alone addresses the root sheet.
            if( requestedRootStripped == wxS( "/" ) )
                requestedRootStripped = wxS( "/" );
        }
    }

    SCH_SHEET_LIST hierarchy = m_frame->Schematic().Hierarchy();
    std::vector<SCH_SHEET_PATH> matches;

    for( const SCH_SHEET_PATH& path : hierarchy )
    {
        wxString kiidPath = path.PathAsString();
        const bool kiidMatch = kiidPath.CmpNoCase( requested ) == 0
                               || kiidPath.CmpNoCase( requestedKiid ) == 0
                               || ( !requestedRootStripped.IsEmpty()
                                    && ( kiidPath.CmpNoCase( requestedRootStripped ) == 0
                                         || humanPath( path ) == requestedRootStripped ) );
        if( humanPath( path ) == requested || kiidMatch )
            matches.push_back( path );
    }

    if( matches.empty() )
    {
        wxString validPaths;
        for( const SCH_SHEET_PATH& path : hierarchy )
        {
            if( !validPaths.IsEmpty() )
                validPaths << wxS( ", " );
            validPaths << wxS( "'" ) << humanPath( path ) << wxS( "'" );
        }

        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        // CONTRACT: "not found" substring + AS_BAD_REQUEST triggers the adapter's
        // disk-reload retry (see CopperV3 mcp_handler_schematic_ops.cpp,
        // ensureSheetActive — coordinate changes to this message).
        e.set_error_message( fmt::format( "sheet '{}' not found; valid sheet paths are: {}",
                                          requested.ToUTF8().data(),
                                          validPaths.ToUTF8().data() ) );
        return tl::unexpected( e );
    }

    if( matches.size() > 1 )
    {
        wxString candidates;
        for( const SCH_SHEET_PATH& path : matches )
        {
            if( !candidates.IsEmpty() )
                candidates << wxS( ", " );
            candidates << wxS( "'" ) << path.PathAsString() << wxS( "'" );
        }

        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "sheet path '{}' is ambiguous; candidates: {}",
                                          requested.ToUTF8().data(),
                                          candidates.ToUTF8().data() ) );
        return tl::unexpected( e );
    }

    SCH_SHEET_PATH target = matches.front();
    NavigateToSheetResponse response;

    auto fillResponse =
            [&]( const SCH_SHEET_PATH& aPath )
            {
                response.set_sheet_path_human( humanPath( aPath ).ToUTF8() );
                response.set_sheet_path_uuid( aPath.PathAsString().ToUTF8() );
                if( SCH_SCREEN* screen = aPath.LastScreen() )
                    response.set_file_name( screen->GetFileName().ToUTF8() );
            };

    if( target == m_frame->GetCurrentSheet() )
    {
        fillResponse( target );
        response.set_changed( false );
        return response;
    }

    SCH_SHEET_PATH before = m_frame->GetCurrentSheet();
    m_frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &target );

    fillResponse( m_frame->GetCurrentSheet() );
    response.set_changed( m_frame->GetCurrentSheet() != before );
    return response;
}




// ---------------------------------------------------------------------------
// Helpers shared by handleCreateBlockSheet / handleAddSheetPort
// ---------------------------------------------------------------------------

namespace {

LABEL_FLAG_SHAPE directionToShape( const std::string& aDir )
{
    if( aDir == "output" )   return LABEL_FLAG_SHAPE::L_OUTPUT;
    if( aDir == "input" )    return LABEL_FLAG_SHAPE::L_INPUT;
    return LABEL_FLAG_SHAPE::L_BIDI;
}

SHEET_SIDE sideFromString( const std::string& aSide )
{
    if( aSide == "right" )  return SHEET_SIDE::RIGHT;
    if( aSide == "top" )    return SHEET_SIDE::TOP;
    if( aSide == "bottom" ) return SHEET_SIDE::BOTTOM;
    return SHEET_SIDE::LEFT;
}

// Add a hierlabel + matching sheet pin for one port.  aSheet must already
// have a screen.  portIndex / portCount are used for auto-Y placement when
// no explicit position is supplied.
void addPortToSheet( SCH_SHEET* aSheet, const std::string& aName,
                     const std::string& aDirection, const std::string& aSide,
                     double aXmm, double aYmm, bool aHasExplicitPos,
                     int aPortIndex, int aPortCount,
                     SCHEMATIC_SETTINGS& aSettings )
{
    LABEL_FLAG_SHAPE shape = directionToShape( aDirection );
    SHEET_SIDE       side  = sideFromString( aSide );

    // ── Hierarchical label in the child screen ─────────────────────────────
    VECTOR2I labelPos;
    if( aHasExplicitPos )
    {
        labelPos = VECTOR2I( KiROUND( aXmm * SCH_IU_PER_MM ),
                             KiROUND( aYmm * SCH_IU_PER_MM ) );
    }
    else
    {
        // Auto-place along the chosen side at 20 mm inset, spacing ports evenly.
        // Child sheet is assumed A4 landscape (297 x 210 mm); use a safe inner margin.
        const int insetX  = KiROUND( 20.0 * SCH_IU_PER_MM );
        const int insetY  = KiROUND( 20.0 * SCH_IU_PER_MM );
        const int spanX   = KiROUND( 257.0 * SCH_IU_PER_MM );
        const int spanY   = KiROUND( 170.0 * SCH_IU_PER_MM );
        int step = ( aPortCount > 1 ) ? ( ( side == SHEET_SIDE::LEFT || side == SHEET_SIDE::RIGHT )
                                          ? spanY / aPortCount
                                          : spanX / aPortCount ) : 0;
        switch( side )
        {
        case SHEET_SIDE::LEFT:
            labelPos = VECTOR2I( insetX, insetY + step * aPortIndex );
            break;
        case SHEET_SIDE::RIGHT:
            labelPos = VECTOR2I( KiROUND( 277.0 * SCH_IU_PER_MM ),
                                 insetY + step * aPortIndex );
            break;
        case SHEET_SIDE::TOP:
            labelPos = VECTOR2I( insetX + step * aPortIndex, insetY );
            break;
        case SHEET_SIDE::BOTTOM:
        default:
            labelPos = VECTOR2I( insetX + step * aPortIndex,
                                 KiROUND( 190.0 * SCH_IU_PER_MM ) );
            break;
        }
    }

    SCH_HIERLABEL* label = new SCH_HIERLABEL( labelPos,
                                              wxString::FromUTF8( aName.c_str() ) );
    label->SetShape( shape );
    label->SetTextSize( VECTOR2I( aSettings.m_DefaultTextSize,
                                  aSettings.m_DefaultTextSize ) );

    // Point the label's arrow toward the matching sheet edge.
    switch( side )
    {
    case SHEET_SIDE::LEFT:   label->SetSpinStyle( SPIN_STYLE::LEFT );   break;
    case SHEET_SIDE::RIGHT:  label->SetSpinStyle( SPIN_STYLE::RIGHT );  break;
    case SHEET_SIDE::TOP:    label->SetSpinStyle( SPIN_STYLE::UP );     break;
    case SHEET_SIDE::BOTTOM: label->SetSpinStyle( SPIN_STYLE::BOTTOM ); break;
    default:                 label->SetSpinStyle( SPIN_STYLE::LEFT );   break;
    }

    aSheet->GetScreen()->Append( label );

    // ── Sheet pin on the sheet symbol in the parent ─────────────────────────
    int pinCount = static_cast<int>( aSheet->GetPins().size() );
    SCH_SHEET_PIN* pin = new SCH_SHEET_PIN( aSheet );
    pin->SetText( wxString::FromUTF8( aName.c_str() ) );
    pin->SetShape( shape );
    pin->SetTextSize( VECTOR2I( aSettings.m_DefaultTextSize,
                                aSettings.m_DefaultTextSize ) );
    pin->SetNumber( pinCount + 1 );

    // Place the pin on the correct edge of the sheet symbol, evenly distributed.
    {
        VECTOR2I sheetPos  = aSheet->GetPosition();
        VECTOR2I sheetSize = aSheet->GetSize();
        int sheetLeft   = sheetPos.x;
        int sheetTop    = sheetPos.y;
        int sheetRight  = sheetLeft + sheetSize.x;
        int sheetBot    = sheetTop  + sheetSize.y;
        int stepPins    = ( aPortCount > 1 )
                          ? ( ( side == SHEET_SIDE::LEFT || side == SHEET_SIDE::RIGHT )
                              ? sheetSize.y / aPortCount
                              : sheetSize.x / aPortCount )
                          : 0;

        VECTOR2I pinPos;
        switch( side )
        {
        case SHEET_SIDE::LEFT:
            pinPos = VECTOR2I( sheetLeft,
                               sheetTop + stepPins / 2 + stepPins * aPortIndex );
            break;
        case SHEET_SIDE::RIGHT:
            pinPos = VECTOR2I( sheetRight,
                               sheetTop + stepPins / 2 + stepPins * aPortIndex );
            break;
        case SHEET_SIDE::TOP:
            pinPos = VECTOR2I( sheetLeft + stepPins / 2 + stepPins * aPortIndex,
                               sheetTop );
            break;
        case SHEET_SIDE::BOTTOM:
        default:
            pinPos = VECTOR2I( sheetLeft + stepPins / 2 + stepPins * aPortIndex,
                               sheetBot );
            break;
        }
        pin->SetSide( side );
        // Clamp Y (or X for top/bottom) inside the sheet boundary after SetSide fixed X.
        if( side == SHEET_SIDE::LEFT || side == SHEET_SIDE::RIGHT )
            pin->SetTextY( pinPos.y );
        else
            pin->SetTextX( pinPos.x );
    }

    aSheet->AddPin( pin );
}

// Evenly distribute every sheet pin along its edge. Pins are grouped by side and
// spaced at edgeLength/n intervals (centered), so the layout stays uniform no
// matter how ports were added (all at once, or one at a time). Preserves each
// pin's current side; only repositions the on-edge axis.
void redistributeSheetPins( SCH_SHEET* aSheet )
{
    std::map<SHEET_SIDE, std::vector<SCH_SHEET_PIN*>> bySide;
    for( SCH_SHEET_PIN* pin : aSheet->GetPins() )
    {
        if( pin )
            bySide[pin->GetSide()].push_back( pin );
    }

    const VECTOR2I pos  = aSheet->GetPosition();
    const VECTOR2I size = aSheet->GetSize();

    for( auto& [side, pins] : bySide )
    {
        const int n = static_cast<int>( pins.size() );
        if( n == 0 )
            continue;

        const bool vertical = ( side == SHEET_SIDE::LEFT || side == SHEET_SIDE::RIGHT );
        const int  edgeLen  = vertical ? size.y : size.x;
        const int  step     = edgeLen / n;

        for( int i = 0; i < n; ++i )
        {
            SCH_SHEET_PIN* pin = pins[i];
            pin->SetSide( side );  // re-fixes the on-edge coordinate to the edge
            const int offset = step / 2 + step * i;
            if( vertical )
                pin->SetTextY( pos.y + offset );
            else
                pin->SetTextX( pos.x + offset );
        }
    }
}

} // anonymous namespace


// ---------------------------------------------------------------------------
// handleCreateBlockSheet
// ---------------------------------------------------------------------------

HANDLER_RESULT<CreateBlockSheetResponse> API_HANDLER_SCH::handleCreateBlockSheet(
        const HANDLER_CONTEXT<CreateBlockSheet>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !m_commits.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BUSY );
        e.set_error_message( "cannot create a block sheet while a commit is in progress; "
                             "end the commit first" );
        return tl::unexpected( e );
    }

    // ── Navigate to the parent sheet ──────────────────────────────────────
    wxString parentPathReq = wxString::FromUTF8( aCtx.Request.parent_sheet_path().c_str() );
    if( parentPathReq.IsEmpty() )
        parentPathReq = wxS( "/" );

    auto humanPath = []( const SCH_SHEET_PATH& aPath ) -> wxString
    {
        return ( aPath.size() == 1 ) ? wxString( wxS( "/" ) ) : aPath.PathHumanReadable( true, true );
    };

    {
        wxString reqKiid = parentPathReq;
        if( !reqKiid.IsEmpty() && !reqKiid.EndsWith( wxS( "/" ) ) )
            reqKiid << wxS( "/" );

        SCH_SHEET_LIST hierarchy = m_frame->Schematic().Hierarchy();
        std::vector<SCH_SHEET_PATH> matches;
        for( const SCH_SHEET_PATH& p : hierarchy )
        {
            if( humanPath( p ) == parentPathReq
                || p.PathAsString() == parentPathReq
                || p.PathAsString() == reqKiid )
                matches.push_back( p );
        }

        if( matches.empty() )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "parent sheet '{}' not found",
                                              parentPathReq.ToUTF8().data() ) );
            return tl::unexpected( e );
        }
        if( matches.size() > 1 )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( fmt::format( "parent sheet path '{}' is ambiguous",
                                              parentPathReq.ToUTF8().data() ) );
            return tl::unexpected( e );
        }

        SCH_SHEET_PATH target = matches.front();
        if( target != m_frame->GetCurrentSheet() )
            m_frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet,
                                                                   &target );
    }

    // ── Validate inputs ────────────────────────────────────────────────────
    std::string sheetName = aCtx.Request.sheet_name();
    if( sheetName.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "sheet_name must not be empty" );
        return tl::unexpected( e );
    }

    // Derive filename from sheet_name if not provided.
    std::string sheetFile = aCtx.Request.sheet_file();
    if( sheetFile.empty() )
    {
        sheetFile.reserve( sheetName.size() + 10 );
        for( char c : sheetName )
            sheetFile += ( c == ' ' ? '_' : std::tolower( (unsigned char) c ) );
        sheetFile += ".kicad_sch";
    }
    else if( sheetFile.find( '.' ) == std::string::npos )
    {
        sheetFile += ".kicad_sch";
    }

    // Refuse early if the project has never been saved — SaveProject() would open
    // a modal file-picker dialog and hang the API client.
    if( m_frame->Prj().IsNullProject() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "schematic has no project; save it once via File > Save before "
                             "creating hierarchical sheets through the API" );
        return tl::unexpected( e );
    }

    // Resolve absolute path using the project root so it works when the adapter
    // runs in a container (no access to the user's filesystem).
    wxString absPath = m_frame->Prj().AbsolutePath( wxString::FromUTF8( sheetFile.c_str() ) );

    // Check for existing sheet with same name or file in hierarchy.
    SCH_SHEET_LIST hierarchy = m_frame->Schematic().Hierarchy();
    for( const SCH_SHEET_PATH& p : hierarchy )
    {
        SCH_SCREEN* screen = p.LastScreen();
        if( !screen ) continue;

        wxFileName existing( screen->GetFileName() );
        if( existing.GetFullPath().CmpNoCase( absPath ) == 0 )
        {
            if( !aCtx.Request.overwrite_existing() )
            {
                ApiResponseStatus e;
                e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                e.set_error_message( fmt::format( "a sheet file '{}' is already in the hierarchy; "
                                                  "set overwrite_existing=true to link it instead",
                                                  sheetFile ) );
                return tl::unexpected( e );
            }
        }
    }

    // ── Dimensions ────────────────────────────────────────────────────────
    double xMm = aCtx.Request.x_mm();
    double yMm = aCtx.Request.y_mm();
    double wMm = aCtx.Request.width_mm()  > 0.0 ? aCtx.Request.width_mm()  : 50.8;
    double hMm = aCtx.Request.height_mm() > 0.0 ? aCtx.Request.height_mm() : 38.1;

    VECTOR2I posIU( KiROUND( xMm * SCH_IU_PER_MM ), KiROUND( yMm * SCH_IU_PER_MM ) );
    VECTOR2I sizeIU( KiROUND( wMm * SCH_IU_PER_MM ), KiROUND( hMm * SCH_IU_PER_MM ) );

    // ── Build the SCH_SHEET object ─────────────────────────────────────────
    SCH_SCREEN* parentScreen = m_frame->GetScreen();
    SCH_SHEET*  sheet = new SCH_SHEET( m_frame->GetCurrentSheet().Last(), posIU );
    sheet->SetScreen( nullptr );
    sheet->SetSize( sizeIU );
    sheet->GetField( FIELD_T::SHEET_NAME )->SetText( wxString::FromUTF8( sheetName.c_str() ) );
    sheet->GetField( FIELD_T::SHEET_FILENAME )->SetText( wxString::FromUTF8( sheetFile.c_str() ) );

    // Create (or link) the child screen.
    if( wxFileExists( absPath ) && aCtx.Request.overwrite_existing() )
    {
        // Link the existing file.
        m_frame->LoadSheetFromFile( sheet, &m_frame->GetCurrentSheet(), absPath,
                                    true /* skip recursion check */, true /* skip lib check */ );
    }
    else
    {
        m_frame->InitSheet( sheet, absPath );
    }

    // Assign a page number.
    SCH_SHEET_PATH instance = m_frame->GetCurrentSheet();
    instance.push_back( sheet );
    wxString pageNum;
    pageNum.Printf( wxT( "%d" ), static_cast<int>( hierarchy.size() ) + 1 );
    instance.SetPageNumber( pageNum );

    // ── Add ports (hierlabels + sheet pins) ───────────────────────────────
    SCHEMATIC_SETTINGS& settings = m_frame->Schematic().Settings();
    int portCount = aCtx.Request.ports_size();
    for( int i = 0; i < portCount; ++i )
    {
        const auto& portDef = aCtx.Request.ports( i );
        if( portDef.name().empty() )
            continue;
        addPortToSheet( sheet, portDef.name(), portDef.direction(), portDef.side(),
                        0.0, 0.0, false /* auto-position */,
                        i, portCount, settings );
    }

    // ── Commit ────────────────────────────────────────────────────────────
    m_frame->AddToScreen( sheet, parentScreen );
    SCH_COMMIT commit( m_frame->GetToolManager() );
    commit.Added( sheet, parentScreen );
    redistributeSheetPins( sheet );
    commit.Push( "Create block sheet" );

    // Save the whole hierarchy so the child .kicad_sch is written to disk.
    if( !m_frame->SaveProject() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "block sheet '{}' was created but could not be saved; "
                                          "try File > Save manually",
                                          sheetFile ) );
        return tl::unexpected( e );
    }

    // ── Build response ────────────────────────────────────────────────────
    // Refresh hierarchy after commit+save.
    SCH_SHEET_LIST newHierarchy = m_frame->Schematic().Hierarchy();
    CreateBlockSheetResponse response;
    response.set_ok( true );
    for( const SCH_SHEET_PATH& p : newHierarchy )
    {
        if( p.LastScreen() && p.LastScreen()->GetFileName() == absPath )
        {
            response.set_sheet_path( humanPath( p ).ToUTF8() );
            response.set_sheet_path_uuid( p.PathAsString().ToUTF8() );
            response.set_file_name( sheetFile );
            response.set_file_path( absPath.ToUTF8() );
            break;
        }
    }

    return response;
}


// ---------------------------------------------------------------------------
// handleAddSheetPort
// ---------------------------------------------------------------------------

HANDLER_RESULT<AddSheetPortResponse> API_HANDLER_SCH::handleAddSheetPort(
        const HANDLER_CONTEXT<AddSheetPort>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !m_commits.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BUSY );
        e.set_error_message( "cannot add a sheet port while a commit is in progress" );
        return tl::unexpected( e );
    }

    if( m_frame->Prj().IsNullProject() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "schematic has no project; save it once via File > Save before "
                             "adding sheet ports through the API" );
        return tl::unexpected( e );
    }

    std::string portName = aCtx.Request.name();
    if( portName.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "name must not be empty" );
        return tl::unexpected( e );
    }

    // ── Find the child sheet object in the hierarchy ───────────────────────
    wxString childPathReq = wxString::FromUTF8( aCtx.Request.sheet_path().c_str() );
    if( childPathReq.IsEmpty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "sheet_path is required" );
        return tl::unexpected( e );
    }

    auto humanPath = []( const SCH_SHEET_PATH& aPath ) -> wxString
    {
        return ( aPath.size() == 1 ) ? wxString( wxS( "/" ) ) : aPath.PathHumanReadable( true, true );
    };

    wxString reqKiid = childPathReq;
    if( !reqKiid.IsEmpty() && !reqKiid.EndsWith( wxS( "/" ) ) )
        reqKiid << wxS( "/" );

    SCH_SHEET_LIST hierarchy = m_frame->Schematic().Hierarchy();
    SCH_SHEET_PATH childPath;
    bool           found = false;
    for( const SCH_SHEET_PATH& p : hierarchy )
    {
        if( humanPath( p ) == childPathReq
            || p.PathAsString() == childPathReq
            || p.PathAsString() == reqKiid )
        {
            childPath = p;
            found = true;
            break;
        }
    }

    if( !found )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "sheet '{}' not found", childPathReq.ToUTF8().data() ) );
        return tl::unexpected( e );
    }

    if( childPath.size() < 2 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "sheet_path must be a child sheet, not the root" );
        return tl::unexpected( e );
    }

    SCH_SHEET* sheet = childPath.Last();
    if( !sheet || !sheet->GetScreen() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "sheet '{}' has no loaded screen",
                                          childPathReq.ToUTF8().data() ) );
        return tl::unexpected( e );
    }

    // Navigate to parent so AddToScreen targets the right screen.
    SCH_SHEET_PATH parentPath = childPath;
    parentPath.pop_back();
    if( parentPath != m_frame->GetCurrentSheet() )
        m_frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet,
                                                               &parentPath );

    // ── Skip if pin already exists and labels are already present ─────────
    bool pinExists = sheet->HasPin( wxString::FromUTF8( portName.c_str() ) );
    bool labelExists = false;
    for( EDA_ITEM* item : sheet->GetScreen()->Items().OfType( SCH_HIER_LABEL_T ) )
    {
        if( static_cast<SCH_HIERLABEL*>( item )->GetText()
            == wxString::FromUTF8( portName.c_str() ) )
        {
            labelExists = true;
            break;
        }
    }

    AddSheetPortResponse response;
    response.set_port_name( portName );
    std::vector<std::string> warnings;

    bool addLabel = aCtx.Request.update_child_label() && !labelExists;
    bool addPin   = aCtx.Request.update_parent_pin()  && !pinExists;

    if( !addLabel && !addPin )
    {
        // Port already exists on both sides. Re-flow the sheet pins so the layout
        // stays even (a no-op when they are already evenly spaced) — this lets the
        // agent tidy a clustered sheet by simply re-adding any existing port.
        SCH_SCREEN* parentScreen = m_frame->GetScreen();
        SCH_COMMIT  commit( m_frame->GetToolManager() );
        commit.Modify( sheet, parentScreen );
        redistributeSheetPins( sheet );
        commit.Push( "Tidy sheet ports" );

        response.set_ok( true );
        warnings.push_back( fmt::format( "port '{}' already exists; re-flowed sheet pin layout",
                                         portName ) );
        for( const auto& w : warnings ) response.add_warnings( w );

        if( !m_frame->SaveProject() )
            response.add_warnings( "pins re-flowed but the project could not be saved" );

        return response;
    }

    SCHEMATIC_SETTINGS& settings = m_frame->Schematic().Settings();
    int existingPins = static_cast<int>( sheet->GetPins().size() );

    bool hasExplicit = ( aCtx.Request.x_mm() != 0.0 || aCtx.Request.y_mm() != 0.0 );
    addPortToSheet( sheet,
                    portName,
                    aCtx.Request.direction(),
                    aCtx.Request.side(),
                    aCtx.Request.x_mm(), aCtx.Request.y_mm(),
                    hasExplicit,
                    existingPins, existingPins + 1,
                    settings );

    // Commit on the parent screen (the sheet symbol lives there).
    SCH_SCREEN* parentScreen = m_frame->GetScreen();
    SCH_COMMIT commit( m_frame->GetToolManager() );
    commit.Modify( sheet, parentScreen );
    redistributeSheetPins( sheet );
    commit.Push( "Add sheet port" );

    if( !m_frame->SaveProject() )
        warnings.push_back( "port added but schematic could not be saved; "
                            "try File > Save manually" );

    // Return pin position.
    for( SCH_SHEET_PIN* pin : sheet->GetPins() )
    {
        if( pin->GetText() == wxString::FromUTF8( portName.c_str() ) )
        {
            response.set_pin_x_mm( pin->GetPosition().x / SCH_IU_PER_MM );
            response.set_pin_y_mm( pin->GetPosition().y / SCH_IU_PER_MM );
            break;
        }
    }

    response.set_ok( true );
    for( const auto& w : warnings ) response.add_warnings( w );
    return response;
}


// ---------------------------------------------------------------------------
// handleGetHierarchy — read the whole hierarchy from the in-memory model.
// No disk reads, no project-root resolution: works regardless of where the
// API client runs (Docker / swarm have no access to the user's filesystem).
// ---------------------------------------------------------------------------

namespace {

std::string shapeToDirString( LABEL_FLAG_SHAPE aShape )
{
    switch( aShape )
    {
    case L_INPUT:    return "input";
    case L_OUTPUT:   return "output";
    case L_BIDI:     return "bidirectional";
    case L_TRISTATE: return "tri_state";
    default:         return "passive";  // L_UNSPECIFIED and flag shapes
    }
}

std::string sheetSideToString( SHEET_SIDE aSide )
{
    switch( aSide )
    {
    case SHEET_SIDE::LEFT:   return "left";
    case SHEET_SIDE::RIGHT:  return "right";
    case SHEET_SIDE::TOP:    return "top";
    case SHEET_SIDE::BOTTOM: return "bottom";
    default:                 return "";
    }
}

std::string spinToSideString( SPIN_STYLE aSpin )
{
    switch( static_cast<int>( aSpin ) )
    {
    case SPIN_STYLE::LEFT:   return "left";
    case SPIN_STYLE::RIGHT:  return "right";
    case SPIN_STYLE::UP:     return "top";
    case SPIN_STYLE::BOTTOM: return "bottom";
    default:                 return "";
    }
}

} // anonymous namespace


HANDLER_RESULT<GetHierarchyResponse> API_HANDLER_SCH::handleGetHierarchy(
        const HANDLER_CONTEXT<GetHierarchy>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    const bool includePorts = aCtx.Request.include_ports();

    GetHierarchyResponse response;
    response.set_ok( true );

    SCHEMATIC&     schematic = m_frame->Schematic();
    SCH_SHEET_LIST hierarchy = schematic.Hierarchy();

    // The root SCH_SHEET's uuid is exactly the segment PathAsString() omits.
    const wxString rootUuid = schematic.Root().m_Uuid.AsString();
    response.set_root_uuid( rootUuid.ToUTF8() );

    if( !m_frame->Prj().IsNullProject() )
    {
        response.set_project_name( m_frame->Prj().GetProjectName().ToUTF8() );
        response.set_project_dir( m_frame->Prj().GetProjectPath().ToUTF8() );
    }

    if( schematic.RootScreen() )
        response.set_root_file_path( schematic.RootScreen()->GetFileName().ToUTF8() );

    response.set_current_sheet_path( m_frame->GetCurrentSheet().PathAsString().ToUTF8() );

    auto humanPath = []( const SCH_SHEET_PATH& aPath ) -> wxString
    {
        return ( aPath.size() <= 1 ) ? wxString( wxS( "/" ) ) : aPath.PathHumanReadable( true, true );
    };

    for( const SCH_SHEET_PATH& path : hierarchy )
    {
        const bool   isRoot = ( path.size() <= 1 );
        SCH_SHEET*   sheet  = path.Last();
        SCH_SCREEN*  screen = path.LastScreen();

        HierarchySheetInfo* info = response.add_sheets();

        if( isRoot )
            info->set_name( "Root" );
        else if( sheet )
            info->set_name( sheet->GetField( FIELD_T::SHEET_NAME )->GetText().ToUTF8() );

        info->set_sheet_path( humanPath( path ).ToUTF8() );

        // sheet_id_path: "/<uuid0>/<uuid1>/..." joining every sheet in the path,
        // root uuid included (matches the disk parser's sheetIdPath form).
        wxString idPath;
        for( unsigned i = 0; i < path.size(); ++i )
            idPath << wxS( "/" ) << path.at( i )->m_Uuid.AsString();
        info->set_sheet_id_path( idPath.ToUTF8() );

        info->set_uuid( ( isRoot ? rootUuid : sheet->m_Uuid.AsString() ).ToUTF8() );
        info->set_depth( static_cast<int>( path.size() ) - 1 );

        if( screen )
        {
            const wxString filePath = screen->GetFileName();
            info->set_file_path( filePath.ToUTF8() );
            info->set_file_name( wxFileName( filePath ).GetFullName().ToUTF8() );
        }

        if( !isRoot && sheet )
        {
            SCH_SHEET_PATH parent = path;
            parent.pop_back();
            info->set_parent_sheet_path( humanPath( parent ).ToUTF8() );
            if( parent.LastScreen() )
                info->set_parent_file_path( parent.LastScreen()->GetFileName().ToUTF8() );

            info->set_x_mm( sheet->GetPosition().x / (double) SCH_IU_PER_MM );
            info->set_y_mm( sheet->GetPosition().y / (double) SCH_IU_PER_MM );
            info->set_width_mm( sheet->GetSize().x / (double) SCH_IU_PER_MM );
            info->set_height_mm( sheet->GetSize().y / (double) SCH_IU_PER_MM );

            if( includePorts )
            {
                for( SCH_SHEET_PIN* pin : sheet->GetPins() )
                {
                    if( !pin )
                        continue;
                    HierarchyPortInfo* port = info->add_parent_pins();
                    port->set_name( pin->GetText().ToUTF8() );
                    port->set_direction( shapeToDirString( pin->GetShape() ) );
                    port->set_side( sheetSideToString( pin->GetSide() ) );
                    port->set_x_mm( pin->GetPosition().x / (double) SCH_IU_PER_MM );
                    port->set_y_mm( pin->GetPosition().y / (double) SCH_IU_PER_MM );
                    port->set_rotation_deg( pin->GetTextAngle().AsDegrees() );
                    port->set_uuid( pin->m_Uuid.AsString().ToUTF8() );
                }
            }
        }

        if( includePorts && screen )
        {
            for( EDA_ITEM* item : screen->Items().OfType( SCH_HIER_LABEL_T ) )
            {
                SCH_HIERLABEL* label = static_cast<SCH_HIERLABEL*>( item );
                HierarchyPortInfo* port = info->add_child_labels();
                port->set_name( label->GetText().ToUTF8() );
                port->set_direction( shapeToDirString( label->GetShape() ) );
                port->set_side( spinToSideString( label->GetSpinStyle() ) );
                port->set_x_mm( label->GetPosition().x / (double) SCH_IU_PER_MM );
                port->set_y_mm( label->GetPosition().y / (double) SCH_IU_PER_MM );
                port->set_rotation_deg( label->GetTextAngle().AsDegrees() );
                port->set_uuid( label->m_Uuid.AsString().ToUTF8() );
            }
        }
    }

    return response;
}


// ---------------------------------------------------------------------------
// Sub-sheet CRUD handlers (move/delete sheets and ports). IPC-backed; act on
// the in-memory model and persist via SaveProject so the agent can fully edit
// hierarchy without any adapter-side filesystem access.
// ---------------------------------------------------------------------------

namespace {

// Resolve a request sheet_path (human "/MCU" or PathAsString id form) to a
// unique SCH_SHEET_PATH. Returns false with aErr set on miss/ambiguity.
bool resolveSheetPathReq( SCH_EDIT_FRAME* aFrame, const std::string& aReqUtf8,
                          SCH_SHEET_PATH& aOut, std::string& aErr )
{
    wxString req = wxString::FromUTF8( aReqUtf8.c_str() );
    if( req.IsEmpty() )
    {
        aErr = "sheet_path is required";
        return false;
    }

    auto humanPath = []( const SCH_SHEET_PATH& p ) -> wxString
    {
        return ( p.size() <= 1 ) ? wxString( wxS( "/" ) ) : p.PathHumanReadable( true, true );
    };

    wxString reqKiid = req;
    if( !reqKiid.EndsWith( wxS( "/" ) ) )
        reqKiid << wxS( "/" );

    SCH_SHEET_LIST hierarchy = aFrame->Schematic().Hierarchy();
    std::vector<SCH_SHEET_PATH> matches;
    for( const SCH_SHEET_PATH& p : hierarchy )
    {
        if( humanPath( p ) == req || p.PathAsString() == req || p.PathAsString() == reqKiid )
            matches.push_back( p );
    }

    if( matches.empty() )
    {
        aErr = fmt::format( "sheet '{}' not found", aReqUtf8 );
        return false;
    }
    if( matches.size() > 1 )
    {
        aErr = fmt::format( "sheet path '{}' is ambiguous", aReqUtf8 );
        return false;
    }
    aOut = matches.front();
    return true;
}

// Place a sheet pin on a side (and optionally an explicit cross-axis position).
// SetSide fixes the on-edge axis; we set the cross axis to the explicit value or
// the middle of the edge.
void placeSheetPinOnSide( SCH_SHEET_PIN* aPin, SCH_SHEET* aSheet, SHEET_SIDE aSide,
                          bool aHasExplicit, double aXmm, double aYmm )
{
    aPin->SetSide( aSide );

    const bool horizontalEdge = ( aSide == SHEET_SIDE::LEFT || aSide == SHEET_SIDE::RIGHT );

    if( aHasExplicit )
    {
        VECTOR2I pos( KiROUND( aXmm * SCH_IU_PER_MM ), KiROUND( aYmm * SCH_IU_PER_MM ) );
        if( horizontalEdge )
            aPin->SetTextY( pos.y );
        else
            aPin->SetTextX( pos.x );
    }
    else
    {
        VECTOR2I sheetPos  = aSheet->GetPosition();
        VECTOR2I sheetSize = aSheet->GetSize();
        if( horizontalEdge )
            aPin->SetTextY( sheetPos.y + sheetSize.y / 2 );
        else
            aPin->SetTextX( sheetPos.x + sheetSize.x / 2 );
    }
}

SCH_SHEET_PIN* findSheetPinByName( SCH_SHEET* aSheet, const std::string& aName )
{
    wxString want = wxString::FromUTF8( aName.c_str() );
    for( SCH_SHEET_PIN* pin : aSheet->GetPins() )
    {
        if( pin && pin->GetText() == want )
            return pin;
    }
    return nullptr;
}

} // anonymous namespace


HANDLER_RESULT<MoveSheetResponse> API_HANDLER_SCH::handleMoveSheet(
        const HANDLER_CONTEXT<MoveSheet>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !m_commits.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BUSY );
        e.set_error_message( "cannot move a sheet while a commit is in progress" );
        return tl::unexpected( e );
    }

    if( m_frame->Prj().IsNullProject() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "schematic has no project; save it once via File > Save first" );
        return tl::unexpected( e );
    }

    SCH_SHEET_PATH path;
    std::string    resolveErr;
    if( !resolveSheetPathReq( m_frame, aCtx.Request.sheet_path(), path, resolveErr ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( resolveErr );
        return tl::unexpected( e );
    }

    if( path.size() < 2 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "sheet_path must name a child sheet, not the root" );
        return tl::unexpected( e );
    }

    SCH_SHEET*     sheet  = path.Last();
    SCH_SHEET_PATH parent = path;
    parent.pop_back();
    if( parent != m_frame->GetCurrentSheet() )
        m_frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &parent );

    SCH_SCREEN* parentScreen = m_frame->GetScreen();

    SCH_COMMIT commit( m_frame->GetToolManager() );
    commit.Modify( sheet, parentScreen );

    VECTOR2I newPos( KiROUND( aCtx.Request.x_mm() * SCH_IU_PER_MM ),
                     KiROUND( aCtx.Request.y_mm() * SCH_IU_PER_MM ) );
    sheet->SetPosition( newPos );

    if( aCtx.Request.width_mm() > 0.0 || aCtx.Request.height_mm() > 0.0 )
    {
        VECTOR2I sz = sheet->GetSize();
        if( aCtx.Request.width_mm() > 0.0 )
            sz.x = KiROUND( aCtx.Request.width_mm() * SCH_IU_PER_MM );
        if( aCtx.Request.height_mm() > 0.0 )
            sz.y = KiROUND( aCtx.Request.height_mm() * SCH_IU_PER_MM );
        sheet->SetSize( sz );

        // After a resize the pins can sit off the new edges — re-flow them.
        redistributeSheetPins( sheet );
    }

    commit.Push( "Move sheet" );

    MoveSheetResponse response;
    response.set_ok( true );
    response.set_sheet_path( ( path.size() <= 1 ? wxString( wxS( "/" ) )
                                                : path.PathHumanReadable( true, true ) ).ToUTF8() );
    response.set_x_mm( sheet->GetPosition().x / (double) SCH_IU_PER_MM );
    response.set_y_mm( sheet->GetPosition().y / (double) SCH_IU_PER_MM );
    response.set_width_mm( sheet->GetSize().x / (double) SCH_IU_PER_MM );
    response.set_height_mm( sheet->GetSize().y / (double) SCH_IU_PER_MM );

    if( !m_frame->SaveProject() )
        response.add_warnings( "sheet moved but the project could not be saved; try File > Save" );

    return response;
}


HANDLER_RESULT<DeleteSheetResponse> API_HANDLER_SCH::handleDeleteSheet(
        const HANDLER_CONTEXT<DeleteSheet>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !m_commits.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BUSY );
        e.set_error_message( "cannot delete a sheet while a commit is in progress" );
        return tl::unexpected( e );
    }

    if( m_frame->Prj().IsNullProject() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "schematic has no project; save it once via File > Save first" );
        return tl::unexpected( e );
    }

    SCH_SHEET_PATH path;
    std::string    resolveErr;
    if( !resolveSheetPathReq( m_frame, aCtx.Request.sheet_path(), path, resolveErr ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( resolveErr );
        return tl::unexpected( e );
    }

    if( path.size() < 2 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "sheet_path must name a child sheet, not the root" );
        return tl::unexpected( e );
    }

    SCH_SHEET* sheet = path.Last();
    wxString   childFile = path.LastScreen() ? path.LastScreen()->GetFileName() : wxString();

    DeleteSheetResponse response;
    response.set_sheet_path( path.PathHumanReadable( true, true ).ToUTF8() );

    SCH_SHEET_PATH parent = path;
    parent.pop_back();
    if( parent != m_frame->GetCurrentSheet() )
        m_frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &parent );

    SCH_SCREEN* parentScreen = m_frame->GetScreen();

    SCH_COMMIT commit( m_frame->GetToolManager() );
    sheet->SetFlags( STRUCT_DELETED );
    commit.Remove( sheet, parentScreen );
    commit.Push( "Delete sheet" );

    m_frame->Schematic().RefreshHierarchy();

    // Optionally remove the child .kicad_sch file — but only if no other sheet
    // instance still references the same file.
    bool fileDeleted = false;
    if( aCtx.Request.delete_file() && !childFile.IsEmpty() )
    {
        bool stillReferenced = false;
        for( const SCH_SHEET_PATH& p : m_frame->Schematic().Hierarchy() )
        {
            if( p.LastScreen() && p.LastScreen()->GetFileName() == childFile )
            {
                stillReferenced = true;
                break;
            }
        }

        if( stillReferenced )
        {
            response.add_warnings( "child file left on disk: still referenced by another sheet" );
        }
        else if( wxFileExists( childFile ) )
        {
            fileDeleted = wxRemoveFile( childFile );
            if( !fileDeleted )
                response.add_warnings( "sheet removed but the child .kicad_sch file could not be deleted" );
        }
    }

    response.set_ok( true );
    response.set_file_deleted( fileDeleted );

    if( !m_frame->SaveProject() )
        response.add_warnings( "sheet removed but the project could not be saved; try File > Save" );

    return response;
}


HANDLER_RESULT<MoveSheetPortResponse> API_HANDLER_SCH::handleMoveSheetPort(
        const HANDLER_CONTEXT<MoveSheetPort>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !m_commits.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BUSY );
        e.set_error_message( "cannot move a sheet port while a commit is in progress" );
        return tl::unexpected( e );
    }

    if( m_frame->Prj().IsNullProject() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "schematic has no project; save it once via File > Save first" );
        return tl::unexpected( e );
    }

    SCH_SHEET_PATH path;
    std::string    resolveErr;
    if( !resolveSheetPathReq( m_frame, aCtx.Request.sheet_path(), path, resolveErr ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( resolveErr );
        return tl::unexpected( e );
    }

    if( path.size() < 2 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "sheet_path must name a child sheet, not the root" );
        return tl::unexpected( e );
    }

    SCH_SHEET*     sheet = path.Last();
    SCH_SHEET_PIN* pin   = findSheetPinByName( sheet, aCtx.Request.name() );
    if( !pin )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "port '{}' not found on {}", aCtx.Request.name(),
                                          aCtx.Request.sheet_path() ) );
        return tl::unexpected( e );
    }

    SCH_SHEET_PATH parent = path;
    parent.pop_back();
    if( parent != m_frame->GetCurrentSheet() )
        m_frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &parent );

    SCH_SCREEN* parentScreen = m_frame->GetScreen();

    SCH_COMMIT commit( m_frame->GetToolManager() );
    commit.Modify( sheet, parentScreen );

    SHEET_SIDE side = aCtx.Request.side().empty() ? pin->GetSide()
                                                  : sideFromString( aCtx.Request.side() );
    const bool hasExplicit = ( aCtx.Request.x_mm() != 0.0 || aCtx.Request.y_mm() != 0.0 );
    placeSheetPinOnSide( pin, sheet, side, hasExplicit, aCtx.Request.x_mm(), aCtx.Request.y_mm() );

    commit.Push( "Move sheet port" );

    MoveSheetPortResponse response;
    response.set_ok( true );
    response.set_name( aCtx.Request.name() );
    response.set_x_mm( pin->GetPosition().x / (double) SCH_IU_PER_MM );
    response.set_y_mm( pin->GetPosition().y / (double) SCH_IU_PER_MM );
    response.set_side( sheetSideToString( pin->GetSide() ) );

    if( !m_frame->SaveProject() )
        response.add_warnings( "port moved but the project could not be saved; try File > Save" );

    return response;
}


HANDLER_RESULT<DeleteSheetPortResponse> API_HANDLER_SCH::handleDeleteSheetPort(
        const HANDLER_CONTEXT<DeleteSheetPort>& aCtx )
{
    if( std::optional<ApiResponseStatus> busy = checkForBusy() )
        return tl::unexpected( *busy );

    if( !m_commits.empty() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BUSY );
        e.set_error_message( "cannot delete a sheet port while a commit is in progress" );
        return tl::unexpected( e );
    }

    if( m_frame->Prj().IsNullProject() )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "schematic has no project; save it once via File > Save first" );
        return tl::unexpected( e );
    }

    SCH_SHEET_PATH path;
    std::string    resolveErr;
    if( !resolveSheetPathReq( m_frame, aCtx.Request.sheet_path(), path, resolveErr ) )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( resolveErr );
        return tl::unexpected( e );
    }

    if( path.size() < 2 )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( "sheet_path must name a child sheet, not the root" );
        return tl::unexpected( e );
    }

    SCH_SHEET*  sheet       = path.Last();
    SCH_SCREEN* childScreen = path.LastScreen();
    wxString    wantName    = wxString::FromUTF8( aCtx.Request.name().c_str() );

    DeleteSheetPortResponse response;
    response.set_name( aCtx.Request.name() );

    bool labelRemoved = false;

    // 1. Remove the matching hierarchical label in the child screen (optional).
    if( aCtx.Request.delete_child_label() && childScreen )
    {
        if( path != m_frame->GetCurrentSheet() )
            m_frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &path );

        SCH_SCREEN* screen = m_frame->GetScreen();
        SCH_HIERLABEL* label = nullptr;
        for( EDA_ITEM* item : screen->Items().OfType( SCH_HIER_LABEL_T ) )
        {
            if( static_cast<SCH_HIERLABEL*>( item )->GetText() == wantName )
            {
                label = static_cast<SCH_HIERLABEL*>( item );
                break;
            }
        }

        if( label )
        {
            SCH_COMMIT childCommit( m_frame->GetToolManager() );
            label->SetFlags( STRUCT_DELETED );
            childCommit.Remove( label, screen );
            childCommit.Push( "Delete hierarchical label" );
            labelRemoved = true;
        }
    }

    // 2. Remove the sheet pin on the parent sheet symbol.
    SCH_SHEET_PATH parent = path;
    parent.pop_back();
    if( parent != m_frame->GetCurrentSheet() )
        m_frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &parent );

    SCH_SCREEN*    parentScreen = m_frame->GetScreen();
    SCH_SHEET_PIN* pin = findSheetPinByName( sheet, aCtx.Request.name() );
    bool           pinRemoved = false;
    if( pin )
    {
        SCH_COMMIT commit( m_frame->GetToolManager() );
        commit.Modify( sheet, parentScreen );
        sheet->RemovePin( pin );
        redistributeSheetPins( sheet );
        commit.Push( "Delete sheet port" );
        pinRemoved = true;
    }

    if( !pinRemoved && !labelRemoved )
    {
        ApiResponseStatus e;
        e.set_status( ApiStatusCode::AS_BAD_REQUEST );
        e.set_error_message( fmt::format( "port '{}' not found on {} (no sheet pin or child label)",
                                          aCtx.Request.name(), aCtx.Request.sheet_path() ) );
        return tl::unexpected( e );
    }

    response.set_ok( true );
    response.set_pin_removed( pinRemoved );
    response.set_label_removed( labelRemoved );

    if( !m_frame->SaveProject() )
        response.add_warnings( "port removed but the project could not be saved; try File > Save" );

    return response;
}
